#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_connection_callbacks_wrapper.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/stream_encoder.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

class DrainAwareConnectionCallbacksWrapperTest : public testing::Test {
protected:
  NiceMock<Http::MockServerConnectionCallbacks> inner_;
};

TEST_F(DrainAwareConnectionCallbacksWrapperTest, ForwardsOnGoAwayAndFiresCallback) {
  std::optional<Http::GoAwayErrorCode> seen_code;
  DrainAwareConnectionCallbacksWrapper wrapper(
      inner_, [&seen_code](Http::GoAwayErrorCode c) { seen_code = c; });

  EXPECT_CALL(inner_, onGoAway(Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Http::GoAwayErrorCode::NoError);
  ASSERT_TRUE(seen_code.has_value());
  EXPECT_EQ(*seen_code, Http::GoAwayErrorCode::NoError);
}

TEST_F(DrainAwareConnectionCallbacksWrapperTest, ForwardsOnGoAwayOtherErrorCode) {
  std::optional<Http::GoAwayErrorCode> seen_code;
  DrainAwareConnectionCallbacksWrapper wrapper(
      inner_, [&seen_code](Http::GoAwayErrorCode c) { seen_code = c; });

  EXPECT_CALL(inner_, onGoAway(Http::GoAwayErrorCode::Other));
  wrapper.onGoAway(Http::GoAwayErrorCode::Other);
  ASSERT_TRUE(seen_code.has_value());
  EXPECT_EQ(*seen_code, Http::GoAwayErrorCode::Other);
}

TEST_F(DrainAwareConnectionCallbacksWrapperTest, ForwardsOnSettings) {
  DrainAwareConnectionCallbacksWrapper wrapper(inner_, nullptr);

  NiceMock<Http::MockReceivedSettings> settings;
  EXPECT_CALL(inner_, onSettings(_));
  wrapper.onSettings(settings);
}

TEST_F(DrainAwareConnectionCallbacksWrapperTest, ForwardsNewStream) {
  DrainAwareConnectionCallbacksWrapper wrapper(inner_, nullptr);

  NiceMock<Http::MockResponseEncoder> encoder;
  NiceMock<Http::MockRequestDecoder> decoder;
  EXPECT_CALL(inner_, newStream(_, false)).WillOnce(testing::ReturnRef(decoder));
  Http::RequestDecoder& result = wrapper.newStream(encoder, false);
  EXPECT_EQ(&result, &decoder);
}

TEST_F(DrainAwareConnectionCallbacksWrapperTest, NullCallbackDoesNotCrash) {
  DrainAwareConnectionCallbacksWrapper wrapper(inner_, nullptr);

  EXPECT_CALL(inner_, onGoAway(Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Http::GoAwayErrorCode::NoError);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
