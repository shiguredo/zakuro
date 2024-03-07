#include "enable_media_with_fake_call.h"

#include <stdio.h>
#include <memory>
#include <string>
#include <utility>

// WebRTC
#include <api/enable_media.h>
#include <media/base/media_engine.h>
#include <pc/media_factory.h>

class MediaFactoryImpl : public webrtc::MediaFactory {
 public:
  MediaFactoryImpl(
      std::unique_ptr<webrtc::MediaFactory> p,
      const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
      const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config)
      : p_(std::move(p)),
        send_config_(send_config),
        receive_config_(receive_config) {}
  MediaFactoryImpl(const MediaFactoryImpl&) = delete;
  MediaFactoryImpl& operator=(const MediaFactoryImpl&) = delete;
  ~MediaFactoryImpl() override = default;

  std::unique_ptr<webrtc::Call> CreateCall(
      const webrtc::CallConfig& config) override {
    webrtc::DegradedCall::TimeScopedNetworkConfig default_config;

    bool send_config_changed =
        memcmp(&send_config_, &default_config, sizeof(default_config)) != 0;
    bool receive_config_changed =
        memcmp(&receive_config_, &default_config, sizeof(default_config)) != 0;

    std::unique_ptr<webrtc::Call> call = webrtc::Call::Create(config);

    if (send_config_changed || receive_config_changed) {
      std::vector<webrtc::DegradedCall::TimeScopedNetworkConfig> send_config = {
          send_config_};
      std::vector<webrtc::DegradedCall::TimeScopedNetworkConfig>
          receive_config = {receive_config_};
      return std::make_unique<webrtc::DegradedCall>(
          std::move(call), send_config, receive_config);
    }

    return call;
  }

  std::unique_ptr<cricket::MediaEngineInterface> CreateMediaEngine(
      const webrtc::Environment& env,
      webrtc::PeerConnectionFactoryDependencies& deps) override {
    return p_->CreateMediaEngine(env, deps);
  }

 private:
  std::unique_ptr<webrtc::MediaFactory> p_;
  webrtc::DegradedCall::TimeScopedNetworkConfig send_config_;
  webrtc::DegradedCall::TimeScopedNetworkConfig receive_config_;
};

void EnableMediaWithFakeCall(
    webrtc::PeerConnectionFactoryDependencies& deps,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config) {
  webrtc::EnableMedia(deps);
  deps.media_factory = std::make_unique<MediaFactoryImpl>(
      std::move(deps.media_factory), send_config, receive_config);
}