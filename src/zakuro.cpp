#include "zakuro.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// WebRTC
#include <api/enable_media.h>
#include <api/video_codecs/video_codec.h>

// Sora C++ SDK
#include <sora/camera_device_capturer.h>
#include <sora/sora_video_encoder_factory.h>

#include "fake_audio_key_trigger.h"
#include "fake_video_capturer.h"
#include "game/game_kuzushi.h"
#include "nop_video_decoder.h"
#include "scenario_player.h"
#include "util.h"
#include "virtual_client.h"
#include "wav_reader.h"
#include "zakuro.h"
#include "zakuro_stats.h"
#include "zakuro_version.h"

Zakuro::Zakuro(ZakuroConfig config) : config_(std::move(config)) {}

const int MESSAGE_SIZE_MIN = 48;
const int MESSAGE_SIZE_MAX = 256 * 1000;
const int BINARY_POOL_SIZE = 1 * 1024 * 1024;

struct DataChannels {
  struct Channel {
    std::string label;
    int interval = 500;
    int size_min = MESSAGE_SIZE_MIN;
    int size_max = MESSAGE_SIZE_MIN;
  };
  std::vector<Channel> channels;
  std::vector<sora::SoraSignalingConfig::DataChannel> schannels;
};

static bool ParseDataChannels(boost::json::value data_channels,
                              DataChannels& m) {
  m = DataChannels();
  boost::json::value& dcs = data_channels;
  if (!dcs.is_array()) {
    std::cout << __LINE__ << std::endl;
    return false;
  }
  for (auto& j : dcs.as_array()) {
    DataChannels::Channel ch;
    sora::SoraSignalingConfig::DataChannel sch;

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
      sch.label = boost::json::value_to<std::string>(it->value());
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
      sch.direction = boost::json::value_to<std::string>(it->value());
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

    // boost::optional<bool> ordered;
    {
      auto it = obj.find("ordered");
      if (it != obj.end()) {
        sch.ordered = boost::json::value_to<bool>(it->value());
      }
    }

    // boost::optional<int32_t> max_packet_life_time;
    {
      auto it = obj.find("max_packet_life_time");
      if (it != obj.end()) {
        sch.max_packet_life_time = boost::json::value_to<int32_t>(it->value());
      }
    }

    // boost::optional<int32_t> max_retransmits;
    {
      auto it = obj.find("max_retransmits");
      if (it != obj.end()) {
        sch.max_retransmits = boost::json::value_to<int32_t>(it->value());
      }
    }

    // boost::optional<std::string> protocol;
    {
      auto it = obj.find("protocol");
      if (it != obj.end()) {
        sch.protocol = boost::json::value_to<std::string>(it->value());
      }
    }

    // boost::optional<bool> compress;
    {
      auto it = obj.find("compress");
      if (it != obj.end()) {
        sch.compress = boost::json::value_to<bool>(it->value());
      }
    }

    // direction が sendonly, sendrecv の場合だけ channels に追加する
    // recvonly の場合は送信する必要が無いので追加しない
    if (direction == "sendonly" || direction == "sendrecv") {
      m.channels.push_back(ch);
    }
    m.schannels.push_back(sch);
  }
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

  auto capturer =
      ([&]() -> rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> {
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
          sora::CameraDeviceCapturerConfig config;
          config.width = size.width;
          config.height = size.height;
          config.fps = config_.framerate;
          config.device_name = config_.video_device;
          return sora::CreateCameraDeviceCapturer(config);
        }
      })();

  if (!capturer && !config_.no_video_device) {
    std::cerr << "[" << config_.name << "] failed to create capturer"
              << std::endl;
    return 1;
  }

  // DataChannel メッセージング
  DataChannels dcs;
  if (!config_.sora_data_channels.is_null()) {
    if (!ParseDataChannels(config_.sora_data_channels, dcs)) {
      std::cerr << "[" << config_.name << "] failed to parse DataChannels"
                << std::endl;
      return 2;
    }
  }

  VirtualClientConfig vc_config;
  sora::SoraSignalingConfig& sora_config = vc_config.sora_config;
  vc_config.capturer = capturer;
  vc_config.max_retry = config_.max_retry;
  vc_config.retry_interval = config_.retry_interval;
  vc_config.no_video_device = config_.no_video_device;
  vc_config.fixed_resolution = config_.fixed_resolution;
  vc_config.priority = config_.priority;
  vc_config.openh264 = config_.openh264;
  vc_config.initial_mute_video = config_.initial_mute_video;
  vc_config.initial_mute_audio = config_.initial_mute_audio;
  if (config_.no_audio_device) {
    vc_config.audio_type = VirtualClientConfig::AudioType::NoAudio;
  } else if (!config_.game.empty() || fake_audio_key_trigger) {
    vc_config.audio_type = VirtualClientConfig::AudioType::External;
    vc_config.render_audio = gam->AddGameAudio(16000);
    vc_config.sample_rate = 16000;
    vc_config.channels = 1;
  } else if (!config_.fake_audio_capture.empty()) {
    WavReader wav_reader;
    int r = wav_reader.Load(config_.fake_audio_capture);
    if (r != 0) {
      std::cerr << "[" << config_.name << "] failed to load fake audio: path="
                << config_.fake_audio_capture << " result=" << r << std::endl;
      return 1;
    }
    vc_config.audio_type = VirtualClientConfig::AudioType::SpecifiedFakeAudio;
    vc_config.fake_audio.reset(new FakeAudioData());
    vc_config.fake_audio->sample_rate = wav_reader.sample_rate;
    vc_config.fake_audio->channels = wav_reader.channels;
    vc_config.fake_audio->data = std::move(wav_reader.data);
  } else {
    vc_config.audio_type =
        VirtualClientConfig::AudioType::AutoGenerateFakeAudio;
  }

  // Sora client context
  sora::SoraClientContextConfig context_config;
  context_config.use_audio_device = false;

  context_config.configure_dependencies =
      [vc =
           vc_config](webrtc::PeerConnectionFactoryDependencies& dependencies) {
        auto adm = dependencies.worker_thread->BlockingCall([&] {
          ZakuroAudioDeviceModuleConfig admconfig;
          admconfig.task_queue_factory = dependencies.task_queue_factory.get();
          if (vc.audio_type == VirtualClientConfig::AudioType::Device) {
#if defined(__linux__)
            webrtc::AudioDeviceModule::AudioLayer audio_layer =
                webrtc::AudioDeviceModule::kLinuxAlsaAudio;
#else
            webrtc::AudioDeviceModule::AudioLayer audio_layer =
                webrtc::AudioDeviceModule::kPlatformDefaultAudio;
#endif
            admconfig.type = ZakuroAudioDeviceModuleConfig::Type::ADM;
            admconfig.adm = webrtc::AudioDeviceModule::Create(
                audio_layer, dependencies.task_queue_factory.get());
          } else if (vc.audio_type == VirtualClientConfig::AudioType::NoAudio) {
            webrtc::AudioDeviceModule::AudioLayer audio_layer =
                webrtc::AudioDeviceModule::kDummyAudio;
            admconfig.type = ZakuroAudioDeviceModuleConfig::Type::ADM;
            admconfig.adm = webrtc::AudioDeviceModule::Create(
                audio_layer, dependencies.task_queue_factory.get());
          } else if (vc.audio_type ==
                     VirtualClientConfig::AudioType::SpecifiedFakeAudio) {
            admconfig.type = ZakuroAudioDeviceModuleConfig::Type::FakeAudio;
            admconfig.fake_audio = vc.fake_audio;
          } else if (vc.audio_type ==
                     VirtualClientConfig::AudioType::AutoGenerateFakeAudio) {
            admconfig.type = ZakuroAudioDeviceModuleConfig::Type::Safari;
          } else if (vc.audio_type ==
                     VirtualClientConfig::AudioType::External) {
            admconfig.type = ZakuroAudioDeviceModuleConfig::Type::External;
            admconfig.render = vc.render_audio;
            admconfig.sample_rate = vc.sample_rate;
            admconfig.channels = vc.channels;
          }
          return ZakuroAudioDeviceModule::Create(std::move(admconfig));
        });
        dependencies.worker_thread->BlockingCall(
            [&] { dependencies.adm = adm; });

        webrtc::EnableMedia(dependencies);
      };

  context_config.video_codec_factory_config.capability_config
      .get_custom_engines = []() {
    // NopVideoDecoder
    sora::VideoCodecCapability::Engine engine(
        sora::VideoCodecImplementation::kCustom_1);
    engine.parameters.custom_engine_name = "NopVideoDecoder";
    engine.codecs.emplace_back(webrtc::kVideoCodecVP8, false, true);
    engine.codecs.emplace_back(webrtc::kVideoCodecVP9, false, true);
    engine.codecs.emplace_back(webrtc::kVideoCodecH264, false, true);
    engine.codecs.emplace_back(webrtc::kVideoCodecH265, false, true);
    engine.codecs.emplace_back(webrtc::kVideoCodecAV1, false, true);
    return std::vector<sora::VideoCodecCapability::Engine>{engine};
  };

  if (sora::CudaContext::CanCreate()) {
    context_config.video_codec_factory_config.capability_config.cuda_context =
        sora::CudaContext::Create();
  }
  if (sora::AMFContext::CanCreate()) {
    context_config.video_codec_factory_config.capability_config.amf_context =
        sora::AMFContext::Create();
  }
  if (!vc_config.openh264.empty()) {
    context_config.video_codec_factory_config.capability_config.openh264_path =
        vc_config.openh264;
  }

  // コーデックプリファレンスの設定
  context_config.video_codec_factory_config.preference =
      std::invoke([this, &context_config]() {
        std::optional<sora::VideoCodecPreference> preference;

        // 個別のコーデックプリファレンスを設定
        auto add_codec_preference =
            [&preference](
                webrtc::VideoCodecType type,
                std::optional<sora::VideoCodecImplementation> encoder,
                std::optional<sora::VideoCodecImplementation> decoder) {
              if (encoder || decoder) {
                if (!preference) {
                  preference = sora::VideoCodecPreference();
                }
                auto& codec = preference->GetOrAdd(type);
                codec.encoder = encoder;
                codec.decoder = decoder;
              }
            };

        add_codec_preference(webrtc::kVideoCodecVP8, config_.vp8_encoder,
                             std::nullopt);
        add_codec_preference(webrtc::kVideoCodecVP9, config_.vp9_encoder,
                             std::nullopt);
        add_codec_preference(webrtc::kVideoCodecH264, config_.h264_encoder,
                             std::nullopt);
        add_codec_preference(webrtc::kVideoCodecH265, config_.h265_encoder,
                             std::nullopt);
        add_codec_preference(webrtc::kVideoCodecAV1, config_.av1_encoder,
                             std::nullopt);

        auto capability = sora::GetVideoCodecCapability(
            context_config.video_codec_factory_config.capability_config);

        // デフォルトのプリファレンスがない場合は、従来の実装を使用
        if (!preference) {
          preference = sora::VideoCodecPreference();
          preference->Merge(sora::CreateVideoCodecPreferenceFromImplementation(
              capability, sora::VideoCodecImplementation::kInternal));
          preference->Merge(sora::CreateVideoCodecPreferenceFromImplementation(
              capability, sora::VideoCodecImplementation::kCiscoOpenH264));
        }

        // デコーダーは常に NopVideoDecoder を使用する
        preference->Merge(sora::CreateVideoCodecPreferenceFromImplementation(
            capability, sora::VideoCodecImplementation::kCustom_1));

        return preference;
      });
  context_config.video_codec_factory_config.create_video_decoder =
      [](sora::VideoCodecImplementation implementation,
         const sora::VideoCodecCapabilityConfig& capability_config,
         webrtc::VideoCodecType type) {
        if (implementation == sora::VideoCodecImplementation::kCustom_1) {
          return std::make_unique<NopVideoDecoder>();
        } else {
          throw "Invalid implementation";
        }
      };

  vc_config.context = sora::SoraClientContext::Create(context_config);

  // signaling URL のバリデーション
  for (const auto& url : config_.sora_signaling_urls) {
    if (url.find("wss://") != 0 && url.find("ws://") != 0) {
      std::cerr << "[" << config_.name << "] Error: Invalid signaling URL: " << url << std::endl;
      std::cerr << "Signaling URL must start with 'ws://' or 'wss://'" << std::endl;
      return 1;
    }
  }

  sora_config.sora_client = ZakuroVersion::GetClientName();
  sora_config.insecure = config_.insecure;
  sora_config.client_cert = config_.client_cert;
  sora_config.client_key = config_.client_key;
  sora_config.signaling_urls = config_.sora_signaling_urls;
  sora_config.channel_id = config_.sora_channel_id;
  sora_config.client_id = config_.sora_client_id;
  sora_config.bundle_id = config_.sora_bundle_id;
  sora_config.disable_signaling_url_randomization =
      config_.sora_disable_signaling_url_randomization;
  sora_config.video = config_.sora_video;
  sora_config.audio = config_.sora_audio;
  sora_config.video_codec_type = config_.sora_video_codec_type;
  sora_config.audio_codec_type = config_.sora_audio_codec_type;
  sora_config.video_bit_rate = config_.sora_video_bit_rate;
  sora_config.audio_bit_rate = config_.sora_audio_bit_rate;
  sora_config.video_vp9_params = config_.sora_video_vp9_params;
  sora_config.video_av1_params = config_.sora_video_av1_params;
  sora_config.video_h264_params = config_.sora_video_h264_params;
  sora_config.video_h265_params = config_.sora_video_h265_params;
  sora_config.metadata = config_.sora_metadata;
  sora_config.signaling_notify_metadata =
      config_.sora_signaling_notify_metadata;
  sora_config.role = config_.sora_role;
  sora_config.simulcast = config_.sora_simulcast;
  sora_config.simulcast_rid = config_.sora_simulcast_rid;
  sora_config.spotlight = config_.sora_spotlight;
  sora_config.spotlight_number = config_.sora_spotlight_number;
  sora_config.spotlight_focus_rid = config_.sora_spotlight_focus_rid;
  sora_config.spotlight_unfocus_rid = config_.sora_spotlight_unfocus_rid;
  sora_config.data_channel_signaling = config_.sora_data_channel_signaling;
  sora_config.data_channel_signaling_timeout =
      config_.sora_data_channel_signaling_timeout;
  sora_config.ignore_disconnect_websocket =
      config_.sora_ignore_disconnect_websocket;
  sora_config.disconnect_wait_timeout = config_.sora_disconnect_wait_timeout;
  sora_config.data_channels = dcs.schannels;
  sora_config.degradation_preference = config_.degradation_preference;

  std::vector<VirtualClientConfig> vc_configs;
  for (int i = 0; i < config_.vcs; i++) {
    vc_configs.push_back(vc_config);
  }

  std::vector<std::shared_ptr<VirtualClient>> vcs;

  {
    boost::asio::io_context ioc{1};
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc.get_executor());

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](const boost::system::error_code&, int) { ioc.stop(); });

    for (auto& config : vc_configs) {
      config.sora_config.io_context = &ioc;
    }

    for (int i = 0; i < config_.vcs; i++) {
      auto vc = VirtualClient::Create(vc_configs[i]);
      vcs.push_back(std::move(vc));
    }

    ScenarioPlayerConfig spc;
    spc.ioc = &ioc;
    spc.gam = gam.get();
    spc.vcs = &vcs;
    spc.binary_pool.reset(new BinaryPool(BINARY_POOL_SIZE));

    // メインのシナリオとは別に、ラベル毎に裏で DataChannel を送信し続けるシナリオを作る
    std::vector<std::tuple<std::string, ScenarioData>> dcs_data;
    for (const auto& ch : dcs.channels) {
      ScenarioData sd;
      sd.Sleep(ch.interval, ch.interval);
      sd.SendDataChannelMessage(ch.label, ch.size_min, ch.size_max);
      dcs_data.push_back(std::make_tuple("scenario-dcs-" + ch.label, sd));
    }

    // 切断と再接続のシナリオを追加する関数
    auto add_reconnect_scenario = [this](ScenarioData& data, bool exit) {
      int op = 0;
      if (config_.duration == 0) {
        data.Sleep(10000, 10000);
        op += 1;
      } else {
        // duration が設定されている場合、duration 秒待って切断し、
        // repeat_interval 秒待ってから再接続する
        data.Sleep((int)(config_.duration * 1000),
                   (int)(config_.duration * 1000));
        data.Disconnect();
        op += 2;
        if (config_.repeat_interval == 0) {
          data.Exit();
          op += 1;
        } else {
          data.Sleep((int)(config_.repeat_interval * 1000),
                     (int)(config_.repeat_interval * 1000));
          data.Reconnect();
          op += 2;
        }
      }
      return op;
    };

    ScenarioPlayer scenario_player(spc);
    ScenarioData data;
    int loop_index;
    if (!fake_audio_key_trigger) {
      data.Reconnect();
      add_reconnect_scenario(data, true);
      loop_index = 1;
    } else if (config_.scenario == "") {
      data.Reconnect();
      for (const auto& d : dcs_data) {
        data.PlaySubScenario(std::get<0>(d), std::get<1>(d), 0);
      }
      ScenarioData sd;
      sd.Sleep(1000, 5000);
      sd.PlayVoiceNumberClient();
      data.PlaySubScenario("scenario-voice-number-client", sd, 0);
      add_reconnect_scenario(data, true);
      loop_index = 1 + dcs_data.size() + 1;
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
      int first_wait_ms = (int)(1000 * i / config_.vcs_hatch_rate);
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

    // 定期的に VirtualClient の stats を取る
    boost::asio::deadline_timer timer(ioc);
    timer.expires_from_now(boost::posix_time::seconds(5));
    std::function<void(const boost::system::error_code& ec)> f;
    f = [&vcs, c = config_, &timer, &f](const boost::system::error_code& ec) {
      if (ec == boost::asio::error::operation_aborted) {
        return;
      }
      std::vector<VirtualClientStats> ss;
      for (auto& vc : vcs) {
        ss.push_back(vc->GetStats());
      }
      c.stats->Set(c.id, c.name, ss);
      timer.expires_from_now(boost::posix_time::seconds(10));
      timer.async_wait(f);
    };
    timer.async_wait(f);

    ioc.run();

    for (auto& vc : vcs) {
      vc->Clear();
    }
  }

  vcs.clear();

  return 0;
}
