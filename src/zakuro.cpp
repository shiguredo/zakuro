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

const int MESSAGE_SIZE_MIN = 16;
const int MESSAGE_SIZE_MAX = 256 * 1000;
const int BINARY_POOL_SIZE = 1 * 1024 * 1024;

struct DataChannelMessaging {
  struct Channel {
    std::string label;
    int interval = 500;
    int size_min = 16;
    int size_max = 16;
  };
  std::vector<Channel> channels;
  boost::json::value remain;
};

static bool ParseDataChannelMessaging(boost::json::value data_channel_messaging,
                                      DataChannelMessaging& m) {
  m = DataChannelMessaging();
  boost::json::value& dcm = data_channel_messaging;
  if (!dcm.is_array()) {
    std::cout << __LINE__ << std::endl;
    return false;
  }
  for (auto& j : dcm.as_array()) {
    DataChannelMessaging::Channel ch;

    if (!j.is_object()) {
      std::cout << __LINE__ << std::endl;
      return false;
    }
    auto& obj = j.as_object();

    // label
    {
      auto it = obj.find("label");
      if (it == obj.end()) {
        std::cout << __LINE__ << std::endl;
        return false;
      }
      if (!it->value().is_string()) {
        std::cout << __LINE__ << std::endl;
        return false;
      }
      ch.label = boost::json::value_to<std::string>(it->value());
    }

    // direction
    std::string direction;
    {
      auto it = obj.find("direction");
      if (it == obj.end()) {
        std::cout << __LINE__ << std::endl;
        return false;
      }
      if (!it->value().is_string()) {
        std::cout << __LINE__ << std::endl;
        return false;
      }
      direction = boost::json::value_to<std::string>(it->value());
    }

    // interval
    {
      auto it = obj.find("interval");
      if (it != obj.end()) {
        if (!it->value().is_number()) {
          std::cout << __LINE__ << std::endl;
          return false;
        }
        ch.interval = boost::json::value_to<int>(it->value());
        if (ch.interval <= 0) {
          std::cout << __LINE__ << std::endl;
          return false;
          obj.erase(it);
        }
      }
    }

    // size-min
    {
      auto it = obj.find("size-min");
      if (it == obj.end()) {
        it = obj.find("size_min");
      }
      if (it != obj.end()) {
        if (!it->value().is_number()) {
          std::cout << __LINE__ << std::endl;
          return false;
        }
        ch.size_min = boost::json::value_to<int>(it->value());
        if (ch.size_min < MESSAGE_SIZE_MIN || ch.size_min > MESSAGE_SIZE_MAX) {
          std::cout << __LINE__ << std::endl;
          return false;
        }
        obj.erase(it);
      }
    }

    // size-max
    {
      auto it = obj.find("size-max");
      if (it == obj.end()) {
        it = obj.find("size_max");
      }
      if (it != obj.end()) {
        if (!it->value().is_number()) {
          return false;
        }
        ch.size_max = boost::json::value_to<int>(it->value());
        if (ch.size_max < MESSAGE_SIZE_MIN || ch.size_max > MESSAGE_SIZE_MAX) {
          return false;
        }
        obj.erase(it);
      }
    }

    if (ch.size_min > ch.size_max) {
      ch.size_max = ch.size_min;
    }

    // direction が sendonly, sendrecv の場合だけ channels に追加する
    // recvonly の場合は送信する必要が無いので追加しない
    if (direction == "sendonly" || direction == "sendrecv") {
      m.channels.push_back(ch);
    }
  }
  m.remain = dcm;
  return true;
}

int Zakuro::Run() {
  std::unique_ptr<GameAudioManager> gam;
  std::unique_ptr<GameKuzushi> kuzushi;
  if (config_.game == "kuzushi") {
    auto size = config_.GetSize();
    gam.reset(new GameAudioManager());
    kuzushi.reset(
        new GameKuzushi(size.width, size.height, gam.get(), config_.key_core));
  }

  bool fake_audio_key_trigger =
      config_.game != "kuzushi" && config_.fake_audio_capture.empty();
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
  rtcm_config.use_dcsctp = config_.use_dcsctp;
  rtcm_config.fake_network_send = config_.fake_network_send;
  rtcm_config.fake_network_receive = config_.fake_network_receive;
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

  // DataChannel メッセージング
  DataChannelMessaging dcm;
  if (!config_.sora_data_channel_messaging.is_null()) {
    if (!ParseDataChannelMessaging(config_.sora_data_channel_messaging, dcm)) {
      std::cerr << "[" << config_.name
                << "] failed to parse DataChannel messaging" << std::endl;
      return 2;
    }
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
    sorac_config.signaling_urls = config_.sora_signaling_urls;
    sorac_config.channel_id = config_.sora_channel_id;
    sorac_config.video = config_.sora_video;
    sorac_config.audio = config_.sora_audio;
    sorac_config.video_codec_type = config_.sora_video_codec_type;
    sorac_config.audio_codec_type = config_.sora_audio_codec_type;
    sorac_config.video_bit_rate = config_.sora_video_bit_rate;
    sorac_config.audio_bit_rate = config_.sora_audio_bit_rate;
    sorac_config.audio_opus_params_clock_rate =
        config_.sora_audio_opus_params_clock_rate;
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
    sorac_config.data_channel_messaging = dcm.remain;

    for (int i = 0; i < config_.vcs; i++) {
      auto vc = std::unique_ptr<VirtualClient>(
          new VirtualClient(ioc, capturer, rtcm_configs[i], sorac_config));
      vcs.push_back(std::move(vc));
    }

    ScenarioPlayerConfig spc;
    spc.ioc = &ioc;
    spc.gam = gam.get();
    spc.vcs = &vcs;
    spc.binary_pool.reset(new BinaryPool(BINARY_POOL_SIZE));

    // メインのシナリオとは別に、ラベル毎に裏で DataChannel を送信し続けるシナリオを作る
    std::vector<std::tuple<std::string, ScenarioData>> dcm_data;
    for (const auto& ch : dcm.channels) {
      ScenarioData sd;
      sd.Sleep(ch.interval, ch.interval);
      sd.SendDataChannelMessage(ch.label, ch.size_min, ch.size_max);
      dcm_data.push_back(std::make_tuple("scenario-dcm-" + ch.label, sd));
    }

    ScenarioPlayer scenario_player(spc);
    ScenarioData data;
    int loop_index;
    if (!fake_audio_key_trigger) {
      data.Reconnect();
      data.Sleep(10000, 10000);
      loop_index = 1;
    } else if (config_.scenario == "") {
      data.Reconnect();
      for (const auto& d : dcm_data) {
        data.PlaySubScenario(std::get<0>(d), std::get<1>(d), 0);
      }
      data.Sleep(1000, 5000);
      data.PlayVoiceNumberClient();
      loop_index = 1 + dcm_data.size();
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
      loop_index = 0;
    }

    for (int i = 0; i < config_.vcs; i++) {
      ScenarioData cdata;
      int first_wait_ms = (int)(1000 * i / config_.hatch_rate);
      cdata.Sleep(first_wait_ms, first_wait_ms);
      cdata.ops.insert(cdata.ops.end(), data.ops.begin(), data.ops.end());
      // 先頭に1個付け足したので +1 する
      const int li = loop_index + 1;
      scenario_player.Play(i, std::move(cdata), li);
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
