#ifndef VIRTUAL_CLIENT_H_
#define VIRTUAL_CLIENT_H_

#include <memory>

#include "rtc/rtc_manager.h"
#include "sora/sora_client.h"

class VirtualClient {
 public:
  VirtualClient(boost::asio::io_context& ioc,
                rtc::scoped_refptr<ScalableVideoTrackSource> capturer,
                RTCManagerConfig rtcm_config,
                SoraClientConfig sorac_config) {
    rtc_manager_.reset(
        new RTCManager(std::move(rtcm_config), std::move(capturer), nullptr));
    sora_client_ =
        SoraClient::Create(ioc, rtc_manager_.get(), std::move(sorac_config));
  }

  void Connect() { sora_client_->Connect(); }
  void Clear() { sora_client_.reset(); }

 private:
  std::unique_ptr<RTCManager> rtc_manager_;
  std::shared_ptr<SoraClient> sora_client_;
};

#endif
