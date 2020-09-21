#ifndef ZAKURO_ARGS_H_
#define ZAKURO_ARGS_H_

#include <iostream>
#include <string>

// nlohmann/json
#include <nlohmann/json.hpp>

struct ZakuroArgs {
  int vcs = 1;
  double hatch_rate = 1.0;

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

  std::string sora_signaling_host = "";
  std::string sora_channel_id;
  bool sora_video = true;
  bool sora_audio = true;
  // 空文字の場合コーデックは Sora 側で決める
  std::string sora_video_codec_type = "";
  std::string sora_audio_codec_type = "";
  // 0 の場合ビットレートは Sora 側で決める
  int sora_video_bit_rate = 0;
  int sora_audio_bit_rate = 0;
  bool sora_auto_connect = false;
  nlohmann::json sora_metadata;
  nlohmann::json sora_signaling_notify_metadata;
  std::string sora_role = "";
  bool sora_multistream = false;
  bool sora_spotlight = false;
  int sora_spotlight_number = 0;
  bool sora_simulcast = false;
  int sora_port = -1;

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

#endif
