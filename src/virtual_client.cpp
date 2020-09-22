#include "virtual_client.h"

VirtualClient::VirtualClient(
    boost::asio::io_context& ioc,
    rtc::scoped_refptr<ScalableVideoTrackSource> capturer,
    RTCManagerConfig rtcm_config,
    SoraClientConfig sorac_config) {
  rtc_manager_.reset(
      new RTCManager(std::move(rtcm_config), std::move(capturer), nullptr));
  sora_client_ =
      SoraClient::Create(ioc, rtc_manager_.get(), std::move(sorac_config));
}

void VirtualClient::Connect() {
  sora_client_->Connect();
}
void VirtualClient::Clear() {
  sora_client_.reset();
}

