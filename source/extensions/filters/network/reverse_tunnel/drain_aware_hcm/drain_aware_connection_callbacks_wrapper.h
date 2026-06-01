#pragma once

#include <functional>

#include "envoy/http/codec.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Wrapper over Http::ServerConnectionCallbacks that fires a user-provided hook on
// peer-initiated GOAWAY in addition to forwarding the callback to the inner callbacks.
// HCM's ConnectionManagerImpl::onGoAway is a no-op, so the inner forward stays a no-op
// today; this wrapper is what gives reverse-tunnel-aware listeners a chance to react
// (e.g. dial a replacement tunnel before the peer's TCP close).
class DrainAwareConnectionCallbacksWrapper : public Http::ServerConnectionCallbacks,
                                             public Logger::Loggable<Logger::Id::filter> {
public:
  using OnPeerGoAwayCb = std::function<void(Http::GoAwayErrorCode)>;

  DrainAwareConnectionCallbacksWrapper(Http::ServerConnectionCallbacks& inner,
                                       OnPeerGoAwayCb on_peer_goaway)
      : inner_(inner), on_peer_goaway_(std::move(on_peer_goaway)) {}

  // Http::ConnectionCallbacks
  void onGoAway(Http::GoAwayErrorCode error_code) override {
    ENVOY_LOG(info, "drain_aware_hcm: peer GOAWAY received (code={})",
              static_cast<int>(error_code));
    if (on_peer_goaway_) {
      on_peer_goaway_(error_code);
    }
    inner_.onGoAway(error_code);
  }
  void onSettings(Http::ReceivedSettings& settings) override { inner_.onSettings(settings); }
  void onMaxStreamsChanged(uint32_t num_streams) override {
    inner_.onMaxStreamsChanged(num_streams);
  }

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder,
                                  bool is_internally_created = false) override {
    return inner_.newStream(response_encoder, is_internally_created);
  }

private:
  Http::ServerConnectionCallbacks& inner_;
  OnPeerGoAwayCb on_peer_goaway_;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
