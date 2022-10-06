#ifndef FAKE_NETWORK_CALL_FACTORY_H_
#define FAKE_NETWORK_CALL_FACTORY_H_

#include <api/call/call_factory_interface.h>
#include <api/sequence_checker.h>
#include <api/test/simulated_network.h>
#include <call/call.h>
#include <call/call_config.h>
#include <call/degraded_call.h>
#include <rtc_base/system/no_unique_address.h>

class FakeNetworkCallFactory : public webrtc::CallFactoryInterface {
 public:
  FakeNetworkCallFactory(
      const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
      const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config);

 private:
  ~FakeNetworkCallFactory() override {}
  webrtc::Call* CreateCall(const webrtc::CallConfig& config) override;

  webrtc::DegradedCall::TimeScopedNetworkConfig send_config_;
  webrtc::DegradedCall::TimeScopedNetworkConfig receive_config_;
};

std::unique_ptr<webrtc::CallFactoryInterface> CreateFakeNetworkCallFactory(
    const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config);

#endif