#include "fake_network_call_factory.h"

#include <stdio.h>
#include <memory>
#include <string>
#include <utility>

// WebRTC
#include <absl/types/optional.h>
#include <api/test/simulated_network.h>
#include <call/call.h>
#include <system_wrappers/include/field_trial.h>

FakeNetworkCallFactory::FakeNetworkCallFactory(
    const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config)
    : send_config_(send_config), receive_config_(receive_config) {}

std::unique_ptr<webrtc::Call> FakeNetworkCallFactory::CreateCall(
    const webrtc::CallConfig& config) {
  webrtc::DegradedCall::TimeScopedNetworkConfig default_config;

  webrtc::RtpTransportConfig transport_config = config.ExtractTransportConfig();

  bool send_config_changed =
      memcmp(&send_config_, &default_config, sizeof(default_config)) != 0;
  bool receive_config_changed =
      memcmp(&receive_config_, &default_config, sizeof(default_config)) != 0;

  auto call = webrtc::Call::Create(
      config, webrtc::Clock::GetRealTimeClock(),
      config.rtp_transport_controller_send_factory->Create(
          transport_config, webrtc::Clock::GetRealTimeClock()));

  if (send_config_changed || receive_config_changed) {
    std::vector<webrtc::DegradedCall::TimeScopedNetworkConfig> send_config = {
        send_config_};
    std::vector<webrtc::DegradedCall::TimeScopedNetworkConfig> receive_config =
        {receive_config_};
    return std::make_unique<webrtc::DegradedCall>(std::move(call), send_config,
                                                  receive_config);
  }

  return call;
}
std::unique_ptr<webrtc::CallFactoryInterface> CreateFakeNetworkCallFactory(
    const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config) {
  return std::unique_ptr<webrtc::CallFactoryInterface>(
      new FakeNetworkCallFactory(send_config, receive_config));
}