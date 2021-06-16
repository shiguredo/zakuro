#include "virtual_client.h"

#include <iostream>

VirtualClient::VirtualClient(
    boost::asio::io_context& ioc,
    rtc::scoped_refptr<ScalableVideoTrackSource> capturer,
    RTCManagerConfig rtcm_config,
    SoraClientConfig sorac_config)
    : ioc_(&ioc),
      capturer_(capturer),
      rtcm_config_(rtcm_config),
      sorac_config_(sorac_config) {}

void VirtualClient::Connect() {
  if (closing_) {
    return;
  }

  if (sora_client_) {
    closing_ = true;
    sora_client_->Close(
        [this, rtc_manager = rtc_manager_, sora_client = sora_client_]() {
          boost::asio::post(*ioc_, [this, rtc_manager, sora_client]() mutable {
            sora_client.reset();
            rtc_manager.reset();
            closing_ = false;
            boost::asio::post(*ioc_, std::bind(&VirtualClient::Connect, this));
          });
        });
    sora_client_.reset();
    rtc_manager_.reset();
    return;
  }

  rtc_manager_.reset(new RTCManager(rtcm_config_, capturer_, nullptr));
  sora_client_ = SoraClient::Create(*ioc_, rtc_manager_, sorac_config_);
  sora_client_->Connect();
}
void VirtualClient::Close() {
  if (sora_client_) {
    closing_ = true;
    sora_client_->Close([this, sora_client = sora_client_]() {
      boost::asio::post(*ioc_, [this, sora_client]() mutable {
        sora_client.reset();
        rtc_manager_.reset();
        closing_ = false;
      });
    });
    sora_client_.reset();
  }
}

void VirtualClient::Clear() {
  sora_client_.reset();
}
