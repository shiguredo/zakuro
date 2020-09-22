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
  void Clear();

 private:
  std::unique_ptr<RTCManager> rtc_manager_;
  std::shared_ptr<SoraClient> sora_client_;
};

#endif
