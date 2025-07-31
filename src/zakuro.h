#ifndef ZAKURO_H_
#define ZAKURO_H_

#include <algorithm>
#include <cstdlib>
#include <string>

// WebRTC
#include <api/rtp_parameters.h>

// Boost
#include <boost/json.hpp>
#include <boost/optional.hpp>

// Sora
#include <sora/sora_video_codec.h>

#include "game/game_key_core.h"

class ZakuroStats;

struct ZakuroConfig {
  int id = 0;
  std::string name = "zakuro";
  int vcs = 1;
  double vcs_hatch_rate = 1.0;
  double duration = 0;
  double repeat_interval = 0;
  int max_retry = 0;
  double retry_interval = 60;

  bool no_video_device = false;
  bool no_audio_device = false;
  std::string video_device = "";
  std::string resolution = "VGA";
  int framerate = 30;
  bool fixed_resolution = false;
  std::string priority = "BALANCE";
  bool insecure = false;
  bool fake_capture_device = true;
  bool sandstorm = false;
  std::string fake_video_capture = "";
  std::string fake_audio_capture = "";
  std::string openh264 = "";
  std::string scenario;
  std::string client_cert;
  std::string client_key;
  bool initial_mute_video = false;
  bool initial_mute_audio = false;
  std::optional<webrtc::DegradationPreference> degradation_preference;

  std::vector<std::string> sora_signaling_urls;
  std::string sora_channel_id;
  std::string sora_client_id;
  std::string sora_bundle_id;
  bool sora_disable_signaling_url_randomization = false;
  bool sora_video = true;
  bool sora_audio = true;
  // 空文字の場合コーデックは Sora 側で決める
  std::string sora_video_codec_type = "";
  std::string sora_audio_codec_type = "";
  // 0 の場合ビットレートは Sora 側で決める
  int sora_video_bit_rate = 0;
  int sora_audio_bit_rate = 0;
  std::string sora_role = "";
  bool sora_simulcast = false;
  std::string sora_simulcast_rid;
  bool sora_spotlight = false;
  int sora_spotlight_number = 0;
  std::string sora_spotlight_focus_rid;
  std::string sora_spotlight_unfocus_rid;
  std::optional<bool> sora_data_channel_signaling;
  int sora_data_channel_signaling_timeout = 180;
  std::optional<bool> sora_ignore_disconnect_websocket;
  int sora_disconnect_wait_timeout = 5;
  boost::json::value sora_metadata;
  boost::json::value sora_signaling_notify_metadata;
  boost::json::value sora_data_channels;
  boost::json::value sora_video_vp9_params;
  boost::json::value sora_video_av1_params;
  boost::json::value sora_video_h264_params;
  boost::json::value sora_video_h265_params;

  // コーデックプリファレンス
  std::optional<sora::VideoCodecImplementation> vp8_encoder;
  std::optional<sora::VideoCodecImplementation> vp9_encoder;
  std::optional<sora::VideoCodecImplementation> av1_encoder;
  std::optional<sora::VideoCodecImplementation> h264_encoder;
  std::optional<sora::VideoCodecImplementation> h265_encoder;

  std::shared_ptr<GameKeyCore> key_core;

  std::shared_ptr<ZakuroStats> stats;

  struct Size {
    int width;
    int height;
  };
  Size GetSize() {
    if (resolution == "QVGA") {
      return {320, 240};
    } else if (resolution == "VGA") {
      return {640, 480};
    } else if (resolution == "HD") {
      return {1280, 720};
    } else if (resolution == "FHD") {
      return {1920, 1080};
    } else if (resolution == "4K") {
      return {3840, 2160};
    }

    // 128x96 みたいな感じのフォーマット
    auto pos = resolution.find('x');
    if (pos == std::string::npos) {
      return {16, 16};
    }
    auto width = std::atoi(resolution.substr(0, pos).c_str());
    auto height = std::atoi(resolution.substr(pos + 1).c_str());
    return {std::max(16, width), std::max(16, height)};
  }
};

class Zakuro {
 public:
  Zakuro(ZakuroConfig config);
  int Run();

 private:
  ZakuroConfig config_;
};

#endif
