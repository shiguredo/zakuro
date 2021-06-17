#include "zakuro.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include "mac_helper/mac_capturer.h"
#else
#include "v4l2_video_capturer/v4l2_video_capturer.h"
#endif

#include "fake_audio_key_trigger.h"
#include "fake_video_capturer.h"
#include "game/game_kuzushi.h"
#include "scenario_player.h"
#include "sora/sora_server.h"
#include "util.h"
#include "virtual_client.h"
#include "wav_reader.h"
#include "zakuro.h"

Zakuro::Zakuro(ZakuroConfig config) : config_(std::move(config)) {}

int Zakuro::Run() {
  std::unique_ptr<GameAudioManager> gam;
  std::unique_ptr<GameKuzushi> kuzushi;
  if (config_.game == "kuzushi") {
    auto size = config_.GetSize();
    gam.reset(new GameAudioManager());
    kuzushi.reset(
        new GameKuzushi(size.width, size.height, gam.get(), config_.key_core));
  }

  bool fake_audio_key_trigger = true;
  std::unique_ptr<FakeAudioKeyTrigger> trigger;
  if (fake_audio_key_trigger) {
    gam.reset(new GameAudioManager());
  }

  auto capturer = ([&]() -> rtc::scoped_refptr<ScalableVideoTrackSource> {
    if (config_.no_video_device) {
      return nullptr;
    }

    auto size = config_.GetSize();
    if (config_.video_device.empty()) {
      FakeVideoCapturerConfig config;
      config.width = size.width;
      config.height = size.height;
      config.fps = config_.framerate;
      if (!config_.game.empty()) {
        config.type = FakeVideoCapturerConfig::Type::External;
        config.render =
            [&kuzushi](BLContext& ctx,
                       std::chrono::high_resolution_clock::time_point now) {
              kuzushi->Render(ctx, now);
            };
      } else if (config_.fake_video_capture.empty()) {
        config.type = config_.sandstorm
                          ? FakeVideoCapturerConfig::Type::Sandstorm
                          : FakeVideoCapturerConfig::Type::Safari;
      } else {
        config.type = FakeVideoCapturerConfig::Type::Y4MFile;
        config.y4m_path = config_.fake_video_capture;
      }
      return FakeVideoCapturer::Create(std::move(config));
    } else {
#if defined(__APPLE__)
      return MacCapturer::Create(size.width, size.height, config_.framerate,
                                 config_.video_device);
#else
      V4L2VideoCapturerConfig config;
      config.video_device = config_.video_device;
      config.width = size.width;
      config.height = size.height;
      config.framerate = config_.framerate;
      return V4L2VideoCapturer::Create(std::move(config));
#endif
    }
  })();

  if (!capturer && !config_.no_video_device) {
    std::cerr << "[" << config_.name << "] failed to create capturer"
              << std::endl;
    return 1;
  }

  RTCManagerConfig rtcm_config;
  rtcm_config.insecure = config_.insecure;
  rtcm_config.no_video_device = config_.no_video_device;
  rtcm_config.fixed_resolution = config_.fixed_resolution;
  rtcm_config.simulcast = config_.sora_simulcast;
  rtcm_config.priority = config_.priority;
  rtcm_config.openh264 = config_.openh264;
  if (config_.no_audio_device) {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::NoAudio;
  } else if (!config_.game.empty()) {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::External;
  } else if (fake_audio_key_trigger) {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::External;
  } else if (!config_.fake_audio_capture.empty()) {
    WavReader wav_reader;
    int r = wav_reader.Load(config_.fake_audio_capture);
    if (r != 0) {
      std::cerr << "[" << config_.name << "] failed to load fake audio: path="
                << config_.fake_audio_capture << " result=" << r << std::endl;
      return 1;
    }
    rtcm_config.audio_type = RTCManagerConfig::AudioType::SpecifiedFakeAudio;
    rtcm_config.fake_audio.reset(new FakeAudioData());
    rtcm_config.fake_audio->sample_rate = wav_reader.sample_rate;
    rtcm_config.fake_audio->channels = wav_reader.channels;
    rtcm_config.fake_audio->data = std::move(wav_reader.data);
  } else {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::AutoGenerateFakeAudio;
  }
  std::vector<RTCManagerConfig> rtcm_configs;
  for (int i = 0; i < config_.vcs; i++) {
    RTCManagerConfig config = rtcm_config;
    if (config.audio_type == RTCManagerConfig::AudioType::External) {
      config.render_audio = gam->AddGameAudio(16000);
      config.sample_rate = 16000;
      config.channels = 1;
    }
    rtcm_configs.push_back(std::move(config));
  }

  std::vector<std::unique_ptr<VirtualClient>> vcs;

  {
    boost::asio::io_context ioc{1};
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc.get_executor());

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](const boost::system::error_code&, int) { ioc.stop(); });

    SoraClientConfig sorac_config;
    sorac_config.insecure = config_.insecure;
    sorac_config.signaling_url = config_.sora_signaling_url;
    sorac_config.channel_id = config_.sora_channel_id;
    sorac_config.video = config_.sora_video;
    sorac_config.audio = config_.sora_audio;
    sorac_config.video_codec_type = config_.sora_video_codec_type;
    sorac_config.audio_codec_type = config_.sora_audio_codec_type;
    sorac_config.video_bit_rate = config_.sora_video_bit_rate;
    sorac_config.audio_bit_rate = config_.sora_audio_bit_rate;
    sorac_config.metadata = config_.sora_metadata;
    sorac_config.signaling_notify_metadata =
        config_.sora_signaling_notify_metadata;
    sorac_config.role = config_.sora_role;
    sorac_config.multistream = config_.sora_multistream;
    sorac_config.spotlight = config_.sora_spotlight;
    sorac_config.spotlight_number = config_.sora_spotlight_number;
    sorac_config.simulcast = config_.sora_simulcast;
    sorac_config.data_channel_signaling = config_.sora_data_channel_signaling;
    sorac_config.data_channel_signaling_timeout =
        config_.sora_data_channel_signaling_timeout;
    sorac_config.ignore_disconnect_websocket =
        config_.sora_ignore_disconnect_websocket;
    sorac_config.disconnect_wait_timeout = config_.sora_disconnect_wait_timeout;

    for (int i = 0; i < config_.vcs; i++) {
      auto vc = std::unique_ptr<VirtualClient>(
          new VirtualClient(ioc, capturer, rtcm_configs[i], sorac_config));
      vcs.push_back(std::move(vc));
    }

    ScenarioPlayer scenario_player(ioc, gam.get(), vcs);
    ScenarioData data;
    int loop_index;
    if (config_.scenario == "") {
      data.Reconnect();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      loop_index = 2;
    } else if (config_.scenario == "reconnect") {
      data.Reconnect();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      data.Sleep(1000, 5000);
      loop_index = 1;
    }

    for (int i = 0; i < config_.vcs; i++) {
      ScenarioData cdata;
      int first_wait_ms = (int)(1000 * i / config_.hatch_rate);
      cdata.Sleep(first_wait_ms, first_wait_ms);
      cdata.ops.insert(cdata.ops.end(), data.ops.begin(), data.ops.end());
      scenario_player.Play(i, std::move(cdata), loop_index);
    }

    if (fake_audio_key_trigger) {
      trigger.reset(new FakeAudioKeyTrigger(ioc, config_.key_core, gam.get(),
                                            &scenario_player, vcs));
    }

    ioc.run();

    for (auto& vc : vcs) {
      vc->Clear();
    }
  }

  vcs.clear();

  return 0;
}
