#include "fake_network_call_factory.h"

#include <stdio.h>
#include <memory>
#include <string>
#include <utility>

// WebRTC
#include <absl/types/optional.h>
#include <api/test/simulated_network.h>
#include <call/call.h>
#include <call/degraded_call.h>
#include <system_wrappers/include/field_trial.h>

FakeNetworkCallFactory::FakeNetworkCallFactory(
    const webrtc::BuiltInNetworkBehaviorConfig& send_config,
    const webrtc::BuiltInNetworkBehaviorConfig& receive_config)
    : send_config_(send_config), receive_config_(receive_config) {}

webrtc::Call* FakeNetworkCallFactory::CreateCall(
    const webrtc::Call::Config& config) {
  webrtc::BuiltInNetworkBehaviorConfig default_config;

  webrtc::RtpTransportConfig transport_config = config.ExtractTransportConfig();

  // デフォルトの設定と違いがあるか調べる
  static_assert(
      std::is_standard_layout<webrtc::BuiltInNetworkBehaviorConfig>::value, "");
  bool send_config_changed =
      memcmp(&send_config_, &default_config, sizeof(default_config)) != 0;
  bool receive_config_changed =
      memcmp(&receive_config_, &default_config, sizeof(default_config)) != 0;

  webrtc::Call* call = webrtc::Call::Create(
      config, webrtc::Clock::GetRealTimeClock(),
      webrtc::SharedModuleThread::Create(
          webrtc::ProcessThread::Create("ModuleProcessThread"), nullptr),
      config.rtp_transport_controller_send_factory->Create(
          transport_config, webrtc::Clock::GetRealTimeClock(),
          webrtc::ProcessThread::Create("PacerThread")));

  if (send_config_changed || receive_config_changed) {
    return new webrtc::DegradedCall(std::unique_ptr<webrtc::Call>(call),
                                    send_config_, receive_config_,
                                    config.task_queue_factory);
  }

  return call;
}
std::unique_ptr<webrtc::CallFactoryInterface> CreateFakeNetworkCallFactory(
    const webrtc::BuiltInNetworkBehaviorConfig& send_config,
    const webrtc::BuiltInNetworkBehaviorConfig& receive_config) {
  return std::unique_ptr<webrtc::CallFactoryInterface>(
      new FakeNetworkCallFactory(send_config, receive_config));
}