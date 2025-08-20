#include "util.h"

#include <cstdlib>
#include <regex>
#include <string>

// CLI11
#include <CLI/CLI.hpp>

// Boost
#include <boost/beast/version.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/json.hpp>
#include <boost/preprocessor/stringize.hpp>

// WebRTC
#include <rtc_base/crypto_random.h>

// Sora
#include <sora/amf_context.h>
#include <sora/cuda_context.h>
#include <sora/sora_video_codec.h>

#include "zakuro.h"
#include "zakuro_version.h"

namespace std {

std::string to_string(std::string str) {
  return str;
}

}  // namespace std

void Util::ParseArgs(const std::vector<std::string>& cargs,
                     std::string& config_file,
                     int& log_level,
                     int& port,
                     std::string& connection_id_stats_file,
                     double& instance_hatch_rate,
                     ZakuroConfig& config,
                     bool ignore_config) {
  std::vector<std::string> args = cargs;
  std::reverse(args.begin(), args.end());

  CLI::App app("Zakuro - WebRTC Load Testing Tool");
  app.option_defaults()->take_last();

  // アプリケーション全体で１個しか存在しない共通オプション
  bool version = false;
  app.add_flag("--version", version, "Show version information");

  bool show_video_codec_capability = false;
  app.add_flag("--show-video-codec-capability", show_video_codec_capability,
               "Show available video codec capability");

  app.add_option("--config", config_file, "JSONC config file path")
      ->check(CLI::ExistingFile);

  auto log_level_map = std::vector<std::pair<std::string, int>>(
      {{"verbose", 0}, {"info", 1}, {"warning", 2}, {"error", 3}, {"none", 4}});
  app.add_option("--log-level", log_level, "Log severity level threshold")
      ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case));
  app.add_option("--port", port, "Port number (default: -1)")
      ->check(CLI::Range(-1, 65535));
  app.add_option("--output-file-connection-id", connection_id_stats_file,
                 "Output to specified file with connection IDs");
  app.add_option("--instance-hatch-rate", instance_hatch_rate,
                 "Spawned instance per seconds (default: 1.0)")
      ->check(CLI::Range(0.1, 100.0));

  // インスタンス毎のオプション
  auto is_valid_resolution = CLI::Validator(
      [](std::string input) -> std::string {
        if (input == "QVGA" || input == "VGA" || input == "HD" ||
            input == "FHD" || input == "4K") {
          return std::string();
        }

        // 数値x数値、というフォーマットになっているか確認する
        std::regex re("^[1-9][0-9]*x[1-9][0-9]*$");
        if (std::regex_match(input, re)) {
          return std::string();
        }

        return "Must be one of QVGA, VGA, HD, FHD, 4K, or "
               "[WIDTH]x[HEIGHT].";
      },
      "");

  auto bool_map = std::vector<std::pair<std::string, bool>>(
      {{"false", false}, {"true", true}});
  auto optional_bool_map =
      std::vector<std::pair<std::string, std::optional<bool>>>(
          {{"false", false}, {"true", true}, {"none", std::nullopt}});

  app.add_option("--name", config.name, "Client Name");
  app.add_option("--vcs", config.vcs, "Virtual Clients (default: 1)")
      ->check(CLI::Range(1, 1000));
  app.add_option("--vcs-hatch-rate", config.vcs_hatch_rate,
                 "Spawned virtual clients per seconds (default: 1.0)")
      ->check(CLI::Range(0.1, 100.0));
  app.add_option("--duration", config.duration,
                 "(Experimental) Duration of virtual client running in seconds "
                 "(if not zero) (default: 0.0)");
  app.add_option("--repeat-interval", config.repeat_interval,
                 "(Experimental) (If duration is set) Interval to reconnect "
                 "after disconnection (default: 0.0)");
  app.add_option(
      "--max-retry", config.max_retry,
      "(Experimental) Max retries when a connection fails (default: 0)");
  app.add_option("--retry-interval", config.retry_interval,
                 "(Experimental) (If max-retry is set) Interval to reconnect "
                 "after connection fails (default: 60)");

  app.add_flag("--no-video-device", config.no_video_device,
               "Do not use video device (default: false)");
  app.add_flag("--no-audio-device", config.no_audio_device,
               "Do not use audio device (default: false)");
  app.add_flag("--fake-capture-device", config.fake_capture_device,
               "Fake Capture Device (default: true)");
  app.add_option("--fake-video-capture", config.fake_video_capture,
                 "Fake Video from File")
      ->check(CLI::ExistingFile);
  app.add_option("--fake-audio-capture", config.fake_audio_capture,
                 "Fake Audio from File")
      ->check(CLI::ExistingFile);
  app.add_flag("--sandstorm", config.sandstorm,
               "Fake Sandstorm Video (default: false)");
#if defined(__APPLE__)
  app.add_option("--video-device", config.video_device,
                 "Use the video device specified by an index or a name "
                 "(use the first one if not specified)");
#elif defined(__linux__)
  app.add_option("--video-device", config.video_device,
                 "Use the video input device specified by a name "
                 "(some device will be used if not specified)")
      ->check(CLI::ExistingFile);
#endif
  app.add_option("--resolution", config.resolution,
                 "Video resolution (one of QVGA, VGA, HD, FHD, 4K, or "
                 "[WIDTH]x[HEIGHT]) (default: VGA)")
      ->check(is_valid_resolution);
  app.add_option("--framerate", config.framerate,
                 "Video framerate (default: 30)")
      ->check(CLI::Range(1, 60));
  app.add_flag("--fixed-resolution", config.fixed_resolution,
               "Maintain video resolution in degradation (default: false)");
  app.add_option(
         "--priority", config.priority,
         "(Experimental) Preference in video degradation (default: BALANCE)")
      ->check(CLI::IsMember({"BALANCE", "FRAMERATE", "RESOLUTION"}));
  app.add_flag(
      "--insecure", config.insecure,
      "Allow insecure server connections when using SSL (default: false)");
  app.add_option("--openh264", config.openh264,
                 "OpenH264 dynamic library path. \"OpenH264 Video Codec "
                 "provided by Cisco Systems, Inc.\"")
      ->check(CLI::ExistingFile);
  app.add_option("--scenario", config.scenario, "Scenario type")
      ->check(CLI::IsMember({"", "reconnect"}));
  app.add_option("--client-cert", config.client_cert,
                 "Cert file path for client certification (PEM format)")
      ->check(CLI::ExistingFile);
  app.add_option("--client-key", config.client_key,
                 "Private key file path for client certification (PEM format)")
      ->check(CLI::ExistingFile);
  app.add_option("--initial-mute-video", config.initial_mute_video,
                 "Mute video initialy (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--initial-mute-audio", config.initial_mute_audio,
                 "Mute audio initialy (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  auto degradation_preference_map =
      std::vector<std::pair<std::string, webrtc::DegradationPreference>>(
          {{"disabled", webrtc::DegradationPreference::DISABLED},
           {"maintain_framerate",
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE},
           {"maintain_resolution",
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION},
           {"balanced", webrtc::DegradationPreference::BALANCED}});
  app.add_option("--degradation-preference", config.degradation_preference,
                 "Degradation preference")
      ->transform(CLI::CheckedTransformer(degradation_preference_map,
                                          CLI::ignore_case));

  // Sora 系オプション
  app.add_option("--sora-signaling-url", config.sora_signaling_urls,
                 "Signaling URLs")
      ->take_all();
  app.add_flag("--sora-disable-signaling-url-randomization",
               config.sora_disable_signaling_url_randomization,
               "Disable random connections to signaling URLs (default: false)");
  app.add_option("--sora-channel-id", config.sora_channel_id, "Channel ID");
  app.add_option("--sora-client-id", config.sora_client_id, "Client ID");
  app.add_option("--sora-bundle-id", config.sora_bundle_id, "Bundle ID");
  app.add_option("--sora-role", config.sora_role, "Role")
      ->check(CLI::IsMember({"sendonly", "recvonly", "sendrecv"}));

  app.add_option("--sora-video", config.sora_video,
                 "Send video to sora (default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-audio", config.sora_audio,
                 "Send audio to sora (default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-video-codec-type", config.sora_video_codec_type,
                 "Video codec for send (default: none)")
      ->check(CLI::IsMember({"", "VP8", "VP9", "AV1", "H264", "H265"}));
  app.add_option("--sora-audio-codec-type", config.sora_audio_codec_type,
                 "Audio codec for send (default: none)")
      ->check(CLI::IsMember({"", "OPUS"}));
  app.add_option("--sora-video-bit-rate", config.sora_video_bit_rate,
                 "Video bit rate (default: none)")
      ->check(CLI::Range(0, 30000));
  app.add_option("--sora-audio-bit-rate", config.sora_audio_bit_rate,
                 "Audio bit rate (default: none)")
      ->check(CLI::Range(0, 510));
  app.add_option("--sora-simulcast", config.sora_simulcast,
                 "Use simulcast (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-simulcast-rid", config.sora_simulcast_rid,
                 "Simulcast rid (default: none)");
  app.add_option("--sora-spotlight", config.sora_spotlight,
                 "Use spotlight (default: none)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-spotlight-number", config.sora_spotlight_number,
                 "Number of spotlight (default: none)")
      ->check(CLI::Range(0, 8));
  app.add_option("--sora-spotlight-focus-rid", config.sora_spotlight_focus_rid,
                 "Spotlight focus rid (default: none)");
  app.add_option("--sora-spotlight-unfocus-rid",
                 config.sora_spotlight_unfocus_rid,
                 "Spotlight unfocus rid (default: none)");
  app.add_option("--sora-data-channel-signaling",
                 config.sora_data_channel_signaling,
                 "Use DataChannel for Sora signaling (default: none)")
      ->type_name("TEXT")
      ->transform(CLI::CheckedTransformer(optional_bool_map, CLI::ignore_case));
  app.add_option("--sora-data-channel-signaling-timeout",
                 config.sora_data_channel_signaling_timeout,
                 "Timeout for Data Channel in seconds (default: 180)")
      ->check(CLI::PositiveNumber);
  app.add_option("--sora-ignore-disconnect-websocket",
                 config.sora_ignore_disconnect_websocket,
                 "Ignore WebSocket disconnection if using Data Channel "
                 "(default: none)")
      ->type_name("TEXT")
      ->transform(CLI::CheckedTransformer(optional_bool_map, CLI::ignore_case));
  app.add_option(
         "--sora-disconnect-wait-timeout", config.sora_disconnect_wait_timeout,
         "Disconnecting timeout for Data Channel in seconds (default: 5)")
      ->check(CLI::PositiveNumber);

  auto is_json = CLI::Validator(
      [](std::string input) -> std::string {
        boost::system::error_code ec;
        boost::json::parse(input, ec);
        if (ec) {
          return "Value " + input + " is not JSON Value";
        }
        return std::string();
      },
      "JSON Value");
  std::string sora_metadata;
  app.add_option("--sora-metadata", sora_metadata,
                 "Signaling metadata used in connect message (default: none)")
      ->check(is_json);
  std::string sora_signaling_notify_metadata;
  app.add_option("--sora-signaling-notify-metadata",
                 sora_signaling_notify_metadata,
                 "Signaling metadata (default: none)")
      ->check(is_json);
  std::string sora_data_channels;
  app.add_option("--sora-data-channels", sora_data_channels,
                 "DataChannels (default: none)")
      ->check(is_json);
  std::string sora_video_vp9_params;
  app.add_option("--sora-video-vp9-params", sora_video_vp9_params,
                 "Parameters for VP9 video codec (default: none)")
      ->check(is_json);
  std::string sora_video_av1_params;
  app.add_option("--sora-video-av1-params", sora_video_av1_params,
                 "Parameters for AV1 video codec (default: none)")
      ->check(is_json);
  std::string sora_video_h264_params;
  app.add_option("--sora-video-h264-params", sora_video_h264_params,
                 "Parameters for H.264 video codec (default: none)")
      ->check(is_json);
  std::string sora_video_h265_params;
  app.add_option("--sora-video-h265-params", sora_video_h265_params,
                 "Parameters for H.265 video codec (default: none)")
      ->check(is_json);

  // ビデオコーデック実装の選択肢
  auto video_codec_implementation_map =
      std::vector<std::pair<std::string, sora::VideoCodecImplementation>>(
          {{"internal", sora::VideoCodecImplementation::kInternal},
           {"cisco_openh264", sora::VideoCodecImplementation::kCiscoOpenH264},
           {"intel_vpl", sora::VideoCodecImplementation::kIntelVpl},
           {"nvidia_video_codec_sdk",
            sora::VideoCodecImplementation::kNvidiaVideoCodecSdk},
           {"amd_amf", sora::VideoCodecImplementation::kAmdAmf}});
  auto video_codec_description =
      "(internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf)";

  // VP8
  app.add_option("--vp8-encoder", config.vp8_encoder,
                 "VP8 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));

  // VP9
  app.add_option("--vp9-encoder", config.vp9_encoder,
                 "VP9 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));

  // AV1
  app.add_option("--av1-encoder", config.av1_encoder,
                 "AV1 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));

  // H264
  app.add_option("--h264-encoder", config.h264_encoder,
                 "H.264 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));

  // H265
  app.add_option("--h265-encoder", config.h265_encoder,
                 "H.265 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));

  try {
    app.parse(args);
  } catch (const CLI::ParseError& e) {
    std::exit(app.exit(e));
  }

  if (version) {
    std::cout << ZakuroVersion::GetClientName() << std::endl;
    std::cout << std::endl;
    std::cout << "WebRTC: " << ZakuroVersion::GetLibwebrtcName() << std::endl;
    std::cout << "Environment: " << ZakuroVersion::GetEnvironmentName()
              << std::endl;
    std::exit(0);
  }

  if (show_video_codec_capability) {
    sora::VideoCodecCapabilityConfig capability_config;

    if (sora::CudaContext::CanCreate()) {
      capability_config.cuda_context = sora::CudaContext::Create();
    }
    if (sora::AMFContext::CanCreate()) {
      capability_config.amf_context = sora::AMFContext::Create();
    }
    // OpenH264 パスが指定されている場合
    // コマンドライン引数は既にパースされているので、config.openh264 に値が入っている
    if (!config.openh264.empty()) {
      capability_config.openh264_path = config.openh264;
    }

    auto capability = sora::GetVideoCodecCapability(capability_config);

    for (const auto& engine : capability.engines) {
      std::cout << "Engine: "
                << boost::json::value_from(engine.name).as_string()
                << std::endl;

      for (const auto& codec : engine.codecs) {
        auto codec_type = boost::json::value_from(codec.type).as_string();
        if (codec.encoder) {
          std::cout << "  - " << codec_type << " Encoder" << std::endl;
        }
        if (codec.decoder) {
          std::cout << "  - " << codec_type << " Decoder" << std::endl;
        }

        // コーデックパラメータの表示
        auto params = boost::json::value_from(codec.parameters);
        if (params.as_object().size() > 0) {
          std::cout << "    - Codec Parameters: "
                    << boost::json::serialize(params) << std::endl;
        }
      }

      // エンジンパラメータの表示
      auto engine_params = boost::json::value_from(engine.parameters);
      if (engine_params.as_object().size() > 0) {
        std::cout << "  - Engine Parameters: "
                  << boost::json::serialize(engine_params) << std::endl;
      }
    }

    std::exit(0);
  }

  // 設定ファイルがある
  if (!ignore_config && !config_file.empty()) {
    return;
  }

  // 必須オプション。
  // add_option()->required() を使うと --version や --config を指定した際に
  // エラーになってしまうので、ここでチェックする
  if (config.sora_signaling_urls.empty()) {
    std::cerr << "--sora-signaling-url is required" << std::endl;
    std::exit(1);
  }
  if (config.sora_channel_id.empty()) {
    std::cerr << "--sora-channel-id is required" << std::endl;
    std::exit(1);
  }
  if (config.sora_role.empty()) {
    std::cerr << "--sora-role is required" << std::endl;
    std::exit(1);
  }

  // --openh264 のパスは絶対パスである必要がある
  if (!config.openh264.empty() && config.openh264[0] != '/') {
    std::cerr << "--openh264 file path must be absolute path" << std::endl;
    std::exit(1);
  }

  // メタデータのパース
  if (!sora_metadata.empty()) {
    config.sora_metadata = boost::json::parse(sora_metadata);
  }
  if (!sora_signaling_notify_metadata.empty()) {
    config.sora_signaling_notify_metadata =
        boost::json::parse(sora_signaling_notify_metadata);
  }
  if (!sora_data_channels.empty()) {
    config.sora_data_channels = boost::json::parse(sora_data_channels);
  }
  if (!sora_video_vp9_params.empty()) {
    config.sora_video_vp9_params = boost::json::parse(sora_video_vp9_params);
  }
  if (!sora_video_av1_params.empty()) {
    config.sora_video_av1_params = boost::json::parse(sora_video_av1_params);
  }
  if (!sora_video_h264_params.empty()) {
    config.sora_video_h264_params = boost::json::parse(sora_video_h264_params);
  }
  if (!sora_video_h265_params.empty()) {
    config.sora_video_h265_params = boost::json::parse(sora_video_h265_params);
  }
}

static std::string ConvertEnv(const std::string& input,
                              const std::map<std::string, std::string>& envs) {
  std::string result;
  std::regex re("\\$\\{(.*?)\\}");
  std::sregex_iterator it(input.begin(), input.end(), re);
  std::sregex_iterator last_it;
  std::sregex_iterator end;
  for (; it != end; ++it) {
    const std::smatch& m = *it;
    result += m.prefix().str();
    std::string name = m[1].str();
    auto mit = envs.find(name);
    if (mit != envs.end()) {
      result += mit->second;
    } else {
      result += m.str();
    }
    last_it = it;
  }
  if (last_it != end) {
    result += last_it->suffix().str();
  } else {
    result = input;
  }
  return result;
}

std::vector<std::vector<std::string>> Util::ParseInstanceToArgs(
    const boost::json::value& inst) {
  std::vector<std::vector<std::string>> argss;

  bool has_error = false;

  int instance_num = 1;
  const auto& obj = inst.as_object();
  auto it = obj.find("instance-num");
  if (obj.contains("instance-num")) {
    instance_num = boost::json::value_to<int>(obj.at("instance-num"));
  }

  for (int i = 0; i < instance_num; i++) {
    std::map<std::string, std::string> envs;
    envs[""] = std::to_string(i + 1);
    std::vector<std::string> args;

    // 値のあるオプション
    auto add_option = [&args, &envs](const boost::json::object& obj,
                                     const std::string& prefix,
                                     const std::string& key) {
      auto it = obj.find(key);
      if (it != obj.end()) {
        args.push_back("--" + prefix + key);
        args.push_back(ConvertEnv(PrimitiveValueToString(it->value()), envs));
      }
    };

    // フラグオプション
    auto add_flag = [&args, &envs](const boost::json::object& obj,
                                   const std::string& prefix,
                                   const std::string& key) {
      auto it = obj.find(key);
      if (it != obj.end() && it->value().is_bool() && it->value().as_bool()) {
        args.push_back("--" + prefix + key);
      }
    };

    // JSONオブジェクトをそのまま渡すオプション
    auto add_json_option = [&args, &envs](const boost::json::object& obj,
                                          const std::string& prefix,
                                          const std::string& key) {
      auto it = obj.find(key);
      if (it != obj.end()) {
        args.push_back("--" + prefix + key);
        args.push_back(boost::json::serialize(it->value()));
      }
    };

    const auto& obj = inst.as_object();

    // 一般オプション
    add_option(obj, "", "name");
    add_option(obj, "", "vcs");
    add_option(obj, "", "vcs-hatch-rate");
    add_option(obj, "", "duration");
    add_option(obj, "", "repeat-interval");
    add_option(obj, "", "max-retry");
    add_option(obj, "", "retry-interval");
    add_flag(obj, "", "no-video-device");
    add_flag(obj, "", "no-audio-device");
    add_flag(obj, "", "fake-capture-device");
    add_option(obj, "", "fake-video-capture");
    add_option(obj, "", "fake-audio-capture");
    add_flag(obj, "", "sandstorm");
    add_option(obj, "", "video-device");
    add_option(obj, "", "resolution");
    add_option(obj, "", "framerate");
    add_flag(obj, "", "fixed-resolution");
    add_option(obj, "", "priority");
    add_flag(obj, "", "insecure");
    add_option(obj, "", "openh264");
    add_option(obj, "", "scenario");
    add_option(obj, "", "client-cert");
    add_option(obj, "", "client-key");
    add_option(obj, "", "initial-mute-video");
    add_option(obj, "", "initial-mute-audio");
    add_option(obj, "", "degradation-preference");

    // コーデックプリファレンス
    add_option(obj, "", "vp8-encoder");
    add_option(obj, "", "vp9-encoder");
    add_option(obj, "", "av1-encoder");
    add_option(obj, "", "h264-encoder");
    add_option(obj, "", "h265-encoder");

    // soraオプション
    auto sora_it = obj.find("sora");
    if (sora_it != obj.end()) {
      const auto& sora_obj = sora_it->value().as_object();

      // --sora-signaling-url: string or string[]
      {
        auto it = sora_obj.find("signaling-url");
        if (it != sora_obj.end()) {
          const auto& value = it->value();
          if (value.is_array()) {
            args.push_back("--sora-signaling-url");
            for (const auto& v : value.as_array()) {
              args.push_back(ConvertEnv(PrimitiveValueToString(v), envs));
            }
          } else if (value.is_string()) {
            args.push_back("--sora-signaling-url");
            args.push_back(ConvertEnv(PrimitiveValueToString(value), envs));
          } else {
            throw std::runtime_error(
                "sora.signaling-url must be string or string[]");
          }
        }
      }

      add_flag(sora_obj, "sora-", "disable-signaling-url-randomization");
      add_option(sora_obj, "sora-", "channel-id");
      add_option(sora_obj, "sora-", "client-id");
      add_option(sora_obj, "sora-", "bundle-id");
      add_option(sora_obj, "sora-", "role");
      add_option(sora_obj, "sora-", "video");
      add_option(sora_obj, "sora-", "audio");
      add_option(sora_obj, "sora-", "video-codec-type");
      add_option(sora_obj, "sora-", "audio-codec-type");
      add_option(sora_obj, "sora-", "video-bit-rate");
      add_option(sora_obj, "sora-", "audio-bit-rate");
      add_option(sora_obj, "sora-", "simulcast");
      add_option(sora_obj, "sora-", "simulcast-rid");
      add_option(sora_obj, "sora-", "spotlight");
      add_option(sora_obj, "sora-", "spotlight-number");
      add_option(sora_obj, "sora-", "spotlight-focus-rid");
      add_option(sora_obj, "sora-", "spotlight-unfocus-rid");
      add_option(sora_obj, "sora-", "data-channel-signaling");
      add_option(sora_obj, "sora-", "data-channel-signaling-timeout");
      add_option(sora_obj, "sora-", "ignore-disconnect-websocket");
      add_option(sora_obj, "sora-", "disconnect-wait-timeout");

      add_json_option(sora_obj, "sora-", "metadata");
      add_json_option(sora_obj, "sora-", "signaling-notify-metadata");
      add_json_option(sora_obj, "sora-", "data-channels");
      add_json_option(sora_obj, "sora-", "video-vp9-params");
      add_json_option(sora_obj, "sora-", "video-av1-params");
      add_json_option(sora_obj, "sora-", "video-h264-params");
      add_json_option(sora_obj, "sora-", "video-h265-params");
    }

    argss.push_back(args);
  }

  return argss;
}

boost::json::value Util::LoadJsoncFile(const std::string& file_path) {
  // ファイルの拡張子を確認
  boost::filesystem::path path(file_path);
  if (path.extension() != ".json" && path.extension() != ".jsonc") {
    throw std::runtime_error("Only .json or .jsonc files are supported. Got: " +
                             file_path);
  }

  // ファイルを読み込む
  std::ifstream file(file_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + file_path);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  // parse_optionsを設定（コメントと末尾カンマを許可）
  boost::json::parse_options opt;
  opt.allow_comments = true;
  opt.allow_trailing_commas = true;

  // Boost JSONでパース
  boost::system::error_code ec;
  boost::json::value result = boost::json::parse(content, ec, {}, opt);

  if (ec) {
    throw std::runtime_error("JSON parse error: " + ec.message());
  }

  return result;
}

std::string Util::GenerateRandomChars() {
  return GenerateRandomChars(32);
}

std::string Util::GenerateRandomChars(size_t length) {
  std::string result;
  webrtc::CreateRandomString(length, &result);
  return result;
}

std::string Util::GenerateRandomNumericChars(size_t length) {
  auto random_numerics = []() -> char {
    const char charset[] = "0123456789";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[std::rand() % max_index];
  };
  std::string result(length, 0);
  std::generate_n(result.begin(), length, random_numerics);
  return result;
}

std::string Util::IceConnectionStateToString(
    webrtc::PeerConnectionInterface::IceConnectionState state) {
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      return "new";
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      return "checking";
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      return "connected";
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      return "completed";
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      return "failed";
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      return "disconnected";
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      return "closed";
    case webrtc::PeerConnectionInterface::kIceConnectionMax:
      return "max";
  }
  return "unknown";
}

std::string Util::PrimitiveValueToString(const boost::json::value& v) {
  if (v.is_string()) {
    return std::string(v.as_string());
  } else if (v.is_primitive()) {
    return boost::json::serialize(v);
  }
  return "";
}

namespace http = boost::beast::http;
using string_view = boost::beast::string_view;

string_view Util::MimeType(string_view path) {
  using boost::beast::iequals;
  auto const ext = [&path] {
    auto const pos = path.rfind(".");
    if (pos == string_view::npos)
      return string_view{};
    return path.substr(pos);
  }();

  if (iequals(ext, ".htm"))
    return "text/html";
  if (iequals(ext, ".html"))
    return "text/html";
  if (iequals(ext, ".php"))
    return "text/html";
  if (iequals(ext, ".css"))
    return "text/css";
  if (iequals(ext, ".txt"))
    return "text/plain";
  if (iequals(ext, ".js"))
    return "application/javascript";
  if (iequals(ext, ".json"))
    return "application/json";
  if (iequals(ext, ".xml"))
    return "application/xml";
  if (iequals(ext, ".swf"))
    return "application/x-shockwave-flash";
  if (iequals(ext, ".flv"))
    return "video/x-flv";
  if (iequals(ext, ".png"))
    return "image/png";
  if (iequals(ext, ".jpe"))
    return "image/jpeg";
  if (iequals(ext, ".jpeg"))
    return "image/jpeg";
  if (iequals(ext, ".jpg"))
    return "image/jpeg";
  if (iequals(ext, ".gif"))
    return "image/gif";
  if (iequals(ext, ".bmp"))
    return "image/bmp";
  if (iequals(ext, ".ico"))
    return "image/vnd.microsoft.icon";
  if (iequals(ext, ".tiff"))
    return "image/tiff";
  if (iequals(ext, ".tif"))
    return "image/tiff";
  if (iequals(ext, ".svg"))
    return "image/svg+xml";
  if (iequals(ext, ".svgz"))
    return "image/svg+xml";
  return "application/text";
}

http::response<http::string_body> Util::BadRequest(
    const http::request<http::string_body>& req,
    string_view why) {
  http::response<http::string_body> res{http::status::bad_request,
                                        req.version()};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = std::string(why);
  res.prepare_payload();
  return res;
}

http::response<http::string_body> Util::NotFound(
    const http::request<http::string_body>& req,
    string_view target) {
  http::response<http::string_body> res{http::status::not_found, req.version()};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = "The resource '" + std::string(target) + "' was not found.";
  res.prepare_payload();
  return res;
}

http::response<http::string_body> Util::ServerError(
    const http::request<http::string_body>& req,
    string_view what) {
  http::response<http::string_body> res{http::status::internal_server_error,
                                        req.version()};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = "An error occurred: '" + std::string(what) + "'";
  res.prepare_payload();
  return res;
}
