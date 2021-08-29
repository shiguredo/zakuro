#ifndef VIRTUAL_CLIENT_H_
#define VIRTUAL_CLIENT_H_

#include <memory>

// Boost
#include <boost/asio/io_context.hpp>

#include "rtc/rtc_manager.h"
#include "rtc/scalable_track_source.h"
#include "sora/sora_client.h"

class VirtualClient {
 public:
  VirtualClient(boost::asio::io_context& ioc,
                rtc::scoped_refptr<ScalableVideoTrackSource> capturer,
                RTCManagerConfig rtcm_config,
                SoraClientConfig sorac_config);

  void Connect();
  void Close();
  void Clear();
  void SendMessage(const std::string& label, const std::string& data);

 private:
  boost::asio::io_context* ioc_;
  rtc::scoped_refptr<ScalableVideoTrackSource> capturer_;
  RTCManagerConfig rtcm_config_;
  SoraClientConfig sorac_config_;
  std::shared_ptr<RTCManager> rtc_manager_;
  std::shared_ptr<SoraClient> sora_client_;
  bool closing_ = false;
};

#endif
