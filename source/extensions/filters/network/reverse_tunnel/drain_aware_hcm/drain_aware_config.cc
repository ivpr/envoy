#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_interface.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_server_connection.h"
#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_connection_callbacks_wrapper.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

Http::ServerConnectionPtr DrainAwareHttpConnectionManagerConfig::createBaseCodec(
    Network::Connection& connection, const Buffer::Instance& data,
    Http::ServerConnectionCallbacks& callbacks, Server::OverloadManager& overload_manager) {
  return HttpConnectionManager::HttpConnectionManagerConfig::createCodec(
      connection, data, callbacks, overload_manager);
}

Http::ServerConnectionPtr DrainAwareHttpConnectionManagerConfig::createCodec(
    Network::Connection& connection, const Buffer::Instance& data,
    Http::ServerConnectionCallbacks& callbacks, Server::OverloadManager& overload_manager) {
  // Recover the ReverseConnectionIOHandle that owns this tunnel by typed cast on the
  // accepted socket's IoHandle. We can't key off connection.localAddress() because the
  // listener exposes its bound rc:// address rather than the underlying socket's local
  // port. With the IOHandle in hand, install a wrapper that asks the IOHandle to drop
  // tracking and re-dial when the peer sends GOAWAY.
  //
  // Opt-in: existing reverse-tunnel deployments must explicitly enable the GOAWAY observer
  // path. When disabled the codec uses the unwrapped HCM callbacks (peer GOAWAY remains a
  // no-op in HCM::onGoAway, matching prior behavior).
  std::unique_ptr<DrainAwareConnectionCallbacksWrapper> wrapping_callbacks;
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.reverse_tunnel_drain_with_goaway")) {
    Envoy::Extensions::Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*
        tunnel_iohandle = nullptr;
    if (connection.getSocket() != nullptr) {
      tunnel_iohandle = dynamic_cast<
          Envoy::Extensions::Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*>(
          &connection.getSocket()->ioHandle());
    }
    if (tunnel_iohandle != nullptr && tunnel_iohandle->parent() != nullptr) {
      auto* owner = tunnel_iohandle->parent();
      std::string connection_key = tunnel_iohandle->connectionKey();
      ENVOY_LOG_MISC(debug, "drain_aware_hcm: wired peer-GOAWAY observer for tunnel key='{}'",
                     connection_key);
      wrapping_callbacks = std::make_unique<DrainAwareConnectionCallbacksWrapper>(
          callbacks, [owner, connection_key](Http::GoAwayErrorCode /*code*/) {
            owner->markTunnelDrainingAndDialReplacement(connection_key);
          });
    }
  }

  Http::ServerConnectionCallbacks& effective_callbacks =
      wrapping_callbacks ? static_cast<Http::ServerConnectionCallbacks&>(*wrapping_callbacks)
                         : callbacks;
  auto codec = createBaseCodec(connection, data, effective_callbacks, overload_manager);
  if (codec == nullptr) {
    return codec;
  }

  // Listener-level DrainDecision (NOT serverFactoryContext().drainManager()):
  // /drain_listeners flips this; the server-wide drain manager only fires on hot-restart.
  return std::make_unique<DrainAwareServerConnection>(
      std::move(codec), connection.dispatcher(), factory_context_.drainDecision(),
      std::move(wrapping_callbacks));
}

absl::StatusOr<Network::FilterFactoryCb>
DrainAwareHttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context) {
  const auto& hcm_config = proto_config.hcm_config();
  auto singletons = HttpConnectionManager::Utility::createSingletons(context);

  absl::Status creation_status = absl::OkStatus();
  auto filter_config = std::make_shared<DrainAwareHttpConnectionManagerConfig>(
      hcm_config, context, *singletons.date_provider_, *singletons.route_config_provider_manager_,
      singletons.scoped_routes_config_provider_manager_.get(), *singletons.tracer_manager_,
      *singletons.filter_config_provider_manager_, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  return [singletons, filter_config, &context](Network::FilterManager& filter_manager) -> void {
    auto& server_context = context.serverFactoryContext();
    Server::OverloadManager& overload_manager = context.listenerInfo().shouldBypassOverloadManager()
                                                    ? server_context.nullOverloadManager()
                                                    : server_context.overloadManager();
    auto hcm = std::make_shared<Http::ConnectionManagerImpl>(
        filter_config, context.drainDecision(), server_context.api().randomGenerator(),
        server_context.httpContext(), server_context.runtime(), server_context.localInfo(),
        server_context.clusterManager(), overload_manager,
        server_context.mainThreadDispatcher().timeSource(), context.listenerInfo().direction());
    filter_manager.addReadFilter(std::move(hcm));
  };
}

REGISTER_FACTORY(DrainAwareHttpConnectionManagerFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
