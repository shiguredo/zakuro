#ifndef FAKE_NETWORK_CALL_FACTORY_H_
#define FAKE_NETWORK_CALL_FACTORY_H_

#include <api/call/call_factory_interface.h>
#include <api/sequence_checker.h>
#include <api/test/simulated_network.h>
#include <call/call.h>
#include <call/call_config.h>
#include <rtc_base/system/no_unique_address.h>

class FakeNetworkCallFactory : public webrtc::CallFactoryInterface {
 public:
  FakeNetworkCallFactory(
      const webrtc::BuiltInNetworkBehaviorConfig& send_config,
      const webrtc::BuiltInNetworkBehaviorConfig& receive_config);

 private:
  ~FakeNetworkCallFactory() override {}
  webrtc::Call* CreateCall(const webrtc::CallConfig& config) override;

  webrtc::BuiltInNetworkBehaviorConfig send_config_;
  webrtc::BuiltInNetworkBehaviorConfig receive_config_;
};

std::unique_ptr<webrtc::CallFactoryInterface> CreateFakeNetworkCallFactory(
    const webrtc::BuiltInNetworkBehaviorConfig& send_config,
    const webrtc::BuiltInNetworkBehaviorConfig& receive_config);

#endif