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

  app.add_option("--config", config_file, "YAML config file path")
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
  app.add_option("--game", config.game, "Play game")
      ->check(CLI::IsMember({"kuzushi"}));
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

  // H264 は --openh264 が指定されてる場合のみ動作する
  if (config.sora_video_codec_type == "H264" && config.openh264.empty()) {
    std::cerr << "Specify --openh264=/path/to/libopenh264.so for H.264 codec"
              << std::endl;
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

template <class T>
static std::string ConvertEnv(const YAML::Node& node,
                              const std::map<std::string, std::string>& envs) {
  // とりあえず文字列として取り出して置換する
  std::string input = node.as<std::string>();
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

  // 変換後、実際に対象の型に置換できるかチェック
  YAML::Node n(result);
  return std::to_string(n.as<T>());
}

std::vector<std::vector<std::string>> Util::NodeToArgs(const YAML::Node& inst) {
  std::vector<std::vector<std::string>> argss;

  bool has_error = false;

  int instance_num = 1;
  {
    const YAML::Node& node = inst["instance-num"];
    if (node) {
      instance_num = node.as<int>();
    }
  }

#define DEF_SCALAR(x, prefix, key, type, type_name)                            \
  try {                                                                        \
    const YAML::Node& node = x[key];                                           \
    if (node) {                                                                \
      if (!node.IsScalar()) {                                                  \
        std::cerr << "\"" key "\" の値は " type_name " である必要があります。" \
                  << std::endl;                                                \
        has_error = true;                                                      \
      } else {                                                                 \
        args.push_back("--" prefix key);                                       \
        args.push_back(ConvertEnv<type>(node, envs));                          \
      }                                                                        \
    }                                                                          \
  } catch (YAML::BadConversion & e) {                                          \
    std::cerr << "\"" key "\" の値は " type_name " である必要があります。"     \
              << std::endl;                                                    \
    has_error = true;                                                          \
  }

#define DEF_STRING(x, prefix, key) \
  DEF_SCALAR(x, prefix, key, std::string, "文字列")
#define DEF_INTEGER(x, prefix, key) DEF_SCALAR(x, prefix, key, int, "整数")
#define DEF_DOUBLE(x, prefix, key) DEF_SCALAR(x, prefix, key, double, "実数")
#define DEF_BOOLEAN(x, prefix, key) DEF_SCALAR(x, prefix, key, bool, "bool値")

#define DEF_FLAG(x, prefix, key)                                        \
  try {                                                                 \
    const YAML::Node& node = x[key];                                    \
    if (node) {                                                         \
      if (!node.IsScalar()) {                                           \
        std::cerr << "\"" key "\" の値は bool値 である必要があります。" \
                  << std::endl;                                         \
        has_error = true;                                               \
      } else {                                                          \
        if (node.as<bool>()) {                                          \
          args.push_back("--" prefix key);                              \
        }                                                               \
      }                                                                 \
    }                                                                   \
  } catch (YAML::BadConversion & e) {                                   \
    std::cerr << "\"" key "\" の値は bool値 である必要があります。"     \
              << std::endl;                                             \
    has_error = true;                                                   \
  }

  for (int i = 0; i < instance_num; i++) {
    std::map<std::string, std::string> envs;
    envs[""] = std::to_string(i + 1);
    std::vector<std::string> args;

    DEF_STRING(inst, "", "name");
    DEF_INTEGER(inst, "", "vcs");
    DEF_DOUBLE(inst, "", "vcs-hatch-rate");
    DEF_DOUBLE(inst, "", "duration");
    DEF_DOUBLE(inst, "", "repeat-interval");
    DEF_INTEGER(inst, "", "max-retry");
    DEF_INTEGER(inst, "", "retry-interval");
    DEF_FLAG(inst, "", "no-video-device");
    DEF_FLAG(inst, "", "no-audio-device");
    DEF_FLAG(inst, "", "fake-capture-device");
    DEF_STRING(inst, "", "fake-video-capture");
    DEF_STRING(inst, "", "fake-audio-capture");
    DEF_FLAG(inst, "", "sandstorm");
    DEF_STRING(inst, "", "video-device");
    DEF_STRING(inst, "", "resolution");
    DEF_INTEGER(inst, "", "framerate");
    DEF_FLAG(inst, "", "fixed-resolution");
    DEF_STRING(inst, "", "priority");
    DEF_FLAG(inst, "", "insecure");
    DEF_STRING(inst, "", "openh264");
    DEF_STRING(inst, "", "game");
    DEF_STRING(inst, "", "scenario");
    DEF_STRING(inst, "", "client-cert");
    DEF_STRING(inst, "", "client-key");
    DEF_BOOLEAN(inst, "", "initial-mute-video");
    DEF_BOOLEAN(inst, "", "initial-mute-audio");
    DEF_STRING(inst, "", "degradation-preference");

    // コーデックプリファレンス
    DEF_STRING(inst, "", "vp8-encoder");
    DEF_STRING(inst, "", "vp8-decoder");
    DEF_STRING(inst, "", "vp9-encoder");
    DEF_STRING(inst, "", "vp9-decoder");
    DEF_STRING(inst, "", "av1-encoder");
    DEF_STRING(inst, "", "av1-decoder");
    DEF_STRING(inst, "", "h264-encoder");
    DEF_STRING(inst, "", "h264-decoder");
    DEF_STRING(inst, "", "h265-encoder");
    DEF_STRING(inst, "", "h265-decoder");

    const YAML::Node& sora = inst["sora"];
    if (sora) {
      // --sora-signaling-url: string or string[]
      {
        try {
          const YAML::Node& node = sora["signaling-url"];
          if (node.IsSequence()) {
            args.push_back("--sora-signaling-url");
            for (auto v : node) {
              args.push_back(ConvertEnv<std::string>(v, envs));
            }
          } else if (node.IsScalar()) {
            DEF_STRING(sora, "sora-", "signaling-url");
          } else {
            throw std::exception();
          }
        } catch (std::exception& e) {
          std::cerr << "signaling-url "
                       "の値は文字列または文字列の配列である必要があります。"
                    << std::endl;
          has_error = true;
        }
      }
      DEF_BOOLEAN(sora, "sora-", "disable-signaling-url-randomization");

      DEF_STRING(sora, "sora-", "channel-id");
      DEF_STRING(sora, "sora-", "client-id");
      DEF_STRING(sora, "sora-", "bundle-id");
      DEF_STRING(sora, "sora-", "role");
      DEF_BOOLEAN(sora, "sora-", "video");
      DEF_BOOLEAN(sora, "sora-", "audio");
      DEF_STRING(sora, "sora-", "video-codec-type");
      DEF_STRING(sora, "sora-", "audio-codec-type");
      DEF_INTEGER(sora, "sora-", "video-bit-rate");
      DEF_INTEGER(sora, "sora-", "audio-bit-rate");
      DEF_BOOLEAN(sora, "sora-", "simulcast");
      DEF_STRING(sora, "sora-", "simulcast-rid");
      DEF_BOOLEAN(sora, "sora-", "spotlight");
      DEF_INTEGER(sora, "sora-", "spotlight-number");
      DEF_STRING(sora, "sora-", "spotlight-focus-rid");
      DEF_STRING(sora, "sora-", "spotlight-unfocus-rid");
      DEF_STRING(sora, "sora-", "data-channel-signaling");
      DEF_INTEGER(sora, "sora-", "data-channel-signaling-timeout");
      DEF_STRING(sora, "sora-", "ignore-disconnect-websocket");
      DEF_INTEGER(sora, "sora-", "disconnect-wait-timeout");
      if (sora["metadata"]) {
        boost::json::value value = NodeToJson(sora["metadata"]);
        args.push_back("--sora-metadata");
        args.push_back(boost::json::serialize(value));
      }
      if (sora["signaling-notify-metadata"]) {
        boost::json::value value =
            NodeToJson(sora["signaling-notify-metadata"]);
        args.push_back("--sora-signaling-notify-metadata");
        args.push_back(boost::json::serialize(value));
      }
      if (sora["data-channels"]) {
        boost::json::value value = NodeToJson(sora["data-channels"]);
        args.push_back("--sora-data-channels");
        args.push_back(boost::json::serialize(value));
      }
      // ビデオコーデックパラメータ
      if (sora["video-vp9-params"]) {
        boost::json::value value = NodeToJson(sora["video-vp9-params"]);
        args.push_back("--sora-video-vp9-params");
        args.push_back(boost::json::serialize(value));
      }
      if (sora["video-av1-params"]) {
        boost::json::value value = NodeToJson(sora["video-av1-params"]);
        args.push_back("--sora-video-av1-params");
        args.push_back(boost::json::serialize(value));
      }
      if (sora["video-h264-params"]) {
        boost::json::value value = NodeToJson(sora["video-h264-params"]);
        args.push_back("--sora-video-h264-params");
        args.push_back(boost::json::serialize(value));
      }
      if (sora["video-h265-params"]) {
        boost::json::value value = NodeToJson(sora["video-h265-params"]);
        args.push_back("--sora-video-h265-params");
        args.push_back(boost::json::serialize(value));
      }
    }

    if (has_error) {
      throw std::exception();
    }
    argss.push_back(args);
  }

  return argss;
}

boost::json::value Util::NodeToJson(const YAML::Node& node) {
  switch (node.Type()) {
    case YAML::NodeType::Null: {
      return nullptr;
    }
    case YAML::NodeType::Scalar: {
      if (node.Tag() == "!") {
        // 文字列
        return node.as<std::string>().c_str();
      } else {
        try {
          // bool値として解釈
          return node.as<bool>();
        } catch (YAML::BadConversion& e) {
          try {
            // ダメだったら整数として解釈
            return node.as<int64_t>();
          } catch (YAML::BadConversion& e) {
            // ダメだったら浮動小数点数として解釈
            try {
              return node.as<double>();
            } catch (YAML::BadConversion& e) {
              // それでもダメなら文字列として解釈する
              return node.as<std::string>().c_str();
            }
          }
        }
      }
    }
    case YAML::NodeType::Sequence: {
      boost::json::array ar;
      for (auto v : node) {
        ar.push_back(NodeToJson(v));
      }
      return ar;
    }
    case YAML::NodeType::Map: {
      boost::json::object obj;
      for (auto p : node) {
        obj[p.first.as<std::string>()] = NodeToJson(p.second);
      }
      return obj;
    }
    case YAML::NodeType::Undefined:
    default:
      throw std::exception();
  }
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
