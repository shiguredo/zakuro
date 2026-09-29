#include "util.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
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
                     std::optional<std::string>& http_host,
                     std::optional<int>& http_port,
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
  app.add_option("--http-host", http_host, "HTTP host address to bind");
  app.add_option("--http-port", http_port, "HTTP port number")
      ->check(CLI::Range(1, 65535));
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
           {"nvidia_video_codec",
            sora::VideoCodecImplementation::kNvidiaVideoCodec},
           {"amd_amf", sora::VideoCodecImplementation::kAmdAmf}});
  auto video_codec_description =
      "(internal,cisco_openh264,intel_vpl,nvidia_video_codec,amd_amf)";

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

  // --client-cert と --client-key は両方指定する必要がある
  bool has_client_cert = !config.client_cert.empty();
  bool has_client_key = !config.client_key.empty();
  if (has_client_cert != has_client_key) {
    std::cerr << "--client-cert and --client-key must be specified together"
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

std::optional<std::vector<std::vector<std::string>>> Util::ParseInstanceToArgs(
    const boost::json::value& inst) {
  // 設定ファイルの instance は必ずオブジェクトである必要がある。
  // as_object() は型が違うと例外を投げるため、先に型を検査する
  if (!inst.is_object()) {
    std::cerr << "instance must be an object" << std::endl;
    return std::nullopt;
  }

  const auto& obj = inst.as_object();

  int instance_num = 1;
  {
    auto it = obj.find("instance-num");
    if (it != obj.end()) {
      // value_to<int> は数値以外に加えて、整数でない値と int の範囲外の値でも
      // 例外を投げるため、例外を投げない try_value_to で受ける
      if (!it->value().is_number()) {
        std::cerr << "instance-num must be a number" << std::endl;
        return std::nullopt;
      }
      auto num = boost::json::try_value_to<int>(it->value());
      if (num.has_error()) {
        std::cerr << "instance-num must be an integer in range" << std::endl;
        return std::nullopt;
      }
      instance_num = *num;
      // 0 以下だとインスタンスが 1 つも起動せず、設定ミスに気付けないためエラーにする
      if (instance_num <= 0) {
        std::cerr << "instance-num must be positive" << std::endl;
        return std::nullopt;
      }
      // 上限が無いと argss の構築でメモリを大量に確保するためエラーにする
      // 上限は --vcs の最大値に合わせる
      if (instance_num > 1000) {
        std::cerr << "instance-num must be 1000 or less" << std::endl;
        return std::nullopt;
      }
    }
  }

  std::vector<std::vector<std::string>> argss;

  for (int i = 0; i < instance_num; i++) {
    std::map<std::string, std::string> envs;
    envs[""] = std::to_string(i + 1);
    std::vector<std::string> args;

    // 型が想定と異なる値を見つけたら true にする。
    // ラムダから呼び出し元の関数を return できないため、
    // エラーはこのフラグに記録してループの最後でまとめて判定する
    bool has_error = false;

    // 値のあるオプション
    // check は値の型が想定どおりかを判定する。想定と異なる場合は設定ミスとして弾く。
    // 型を検査しないと、オブジェクトや配列が空文字列に潰れて CLI11 の検証を
    // すり抜けるものがあるため
    auto add_option = [&args, &envs, &has_error](const boost::json::object& obj,
                                                 const std::string& prefix,
                                                 const std::string& key,
                                                 auto check) {
      auto it = obj.find(key);
      if (it == obj.end()) {
        return;
      }
      if (!check(it->value())) {
        std::cerr << prefix << key << " has an unexpected value type"
                  << std::endl;
        has_error = true;
        return;
      }
      args.push_back("--" + prefix + key);
      args.push_back(ConvertEnv(PrimitiveValueToString(it->value()), envs));
    };

    // フラグオプション
    // 真偽値以外はフラグとして扱えないため、型が違えば設定ミスとして弾く
    auto add_flag = [&args, &has_error](const boost::json::object& obj,
                                        const std::string& prefix,
                                        const std::string& key) {
      auto it = obj.find(key);
      if (it == obj.end()) {
        return;
      }
      if (!it->value().is_bool()) {
        std::cerr << prefix << key << " must be a boolean" << std::endl;
        has_error = true;
        return;
      }
      if (it->value().as_bool()) {
        args.push_back("--" + prefix + key);
      }
    };

    // JSONオブジェクトをそのまま渡すオプション
    auto add_json_option = [&args](const boost::json::object& obj,
                                   const std::string& prefix,
                                   const std::string& key) {
      auto it = obj.find(key);
      if (it != obj.end()) {
        args.push_back("--" + prefix + key);
        args.push_back(boost::json::serialize(it->value()));
      }
    };

    // 数値を取るオプションの型判定
    // 整数を取るオプションに実数を指定すると "2E0" のような
    // 不可解な引数になるため、ここで弾く
    auto is_number = [](const boost::json::value& value) {
      return value.is_number();
    };
    // 文字列を取るオプションの型判定 (列挙値の検証は CLI11 に委ねる)
    auto is_string = [](const boost::json::value& value) {
      return value.is_string();
    };
    // 真偽値を取るオプションの型判定
    // JSON の数値 1 や文字列 "true" も CLI11 の CheckedTransformer は通すため、
    // JSON の真偽値だけを受け付ける
    auto is_bool = [](const boost::json::value& value) {
      return value.is_bool();
    };

    // 一般オプション
    add_option(obj, "", "name", is_string);
    add_option(obj, "", "vcs", is_number);
    add_option(obj, "", "vcs-hatch-rate", is_number);
    add_option(obj, "", "duration", is_number);
    add_option(obj, "", "repeat-interval", is_number);
    add_option(obj, "", "max-retry", is_number);
    add_option(obj, "", "retry-interval", is_number);
    add_flag(obj, "", "no-video-device");
    add_flag(obj, "", "no-audio-device");
    add_flag(obj, "", "fake-capture-device");
    add_option(obj, "", "fake-video-capture", is_string);
    add_option(obj, "", "fake-audio-capture", is_string);
    add_flag(obj, "", "sandstorm");
    add_option(obj, "", "video-device", is_string);
    add_option(obj, "", "resolution", is_string);
    add_option(obj, "", "framerate", is_number);
    add_flag(obj, "", "fixed-resolution");
    add_option(obj, "", "priority", is_string);
    add_flag(obj, "", "insecure");
    add_option(obj, "", "openh264", is_string);
    add_option(obj, "", "scenario", is_string);
    add_option(obj, "", "client-cert", is_string);
    add_option(obj, "", "client-key", is_string);
    // initial-mute-video / initial-mute-audio は CLI 側が値付きのオプション
    // (--initial-mute-video true) なので、フラグではなく値として渡す
    add_option(obj, "", "initial-mute-video", is_bool);
    add_option(obj, "", "initial-mute-audio", is_bool);
    add_option(obj, "", "degradation-preference", is_string);

    // コーデックプリファレンス
    add_option(obj, "", "vp8-encoder", is_string);
    add_option(obj, "", "vp9-encoder", is_string);
    add_option(obj, "", "av1-encoder", is_string);
    add_option(obj, "", "h264-encoder", is_string);
    add_option(obj, "", "h265-encoder", is_string);

    // soraオプション
    auto sora_it = obj.find("sora");
    if (sora_it != obj.end()) {
      // as_object() は型が違うと例外を投げるため、先に型を検査する
      if (!sora_it->value().is_object()) {
        std::cerr << "sora must be an object" << std::endl;
        return std::nullopt;
      }
      const auto& sora_obj = sora_it->value().as_object();

      // --sora-signaling-url: string or string[]
      {
        auto it = sora_obj.find("signaling-url");
        if (it != sora_obj.end()) {
          const auto& value = it->value();
          if (value.is_array()) {
            // 空配列は値の無い --sora-signaling-url を組み立てて後続の引数を
            // 食ってしまうため、設定ミスとして弾く
            if (value.as_array().empty()) {
              std::cerr << "sora.signaling-url must not be empty" << std::endl;
              return std::nullopt;
            }
            // 配列要素も文字列であることを検査する。
            // 文字列以外は空文字列に潰れて設定ミスに気付けないため
            for (const auto& v : value.as_array()) {
              if (!v.is_string()) {
                std::cerr << "sora.signaling-url must be string or string[]"
                          << std::endl;
                return std::nullopt;
              }
            }
            args.push_back("--sora-signaling-url");
            for (const auto& v : value.as_array()) {
              args.push_back(ConvertEnv(PrimitiveValueToString(v), envs));
            }
          } else if (value.is_string()) {
            args.push_back("--sora-signaling-url");
            args.push_back(ConvertEnv(PrimitiveValueToString(value), envs));
          } else {
            std::cerr << "sora.signaling-url must be string or string[]"
                      << std::endl;
            return std::nullopt;
          }
        }
      }

      add_flag(sora_obj, "sora-", "disable-signaling-url-randomization");
      add_option(sora_obj, "sora-", "channel-id", is_string);
      add_option(sora_obj, "sora-", "client-id", is_string);
      add_option(sora_obj, "sora-", "bundle-id", is_string);
      add_option(sora_obj, "sora-", "role", is_string);
      add_option(sora_obj, "sora-", "video", is_bool);
      add_option(sora_obj, "sora-", "audio", is_bool);
      add_option(sora_obj, "sora-", "video-codec-type", is_string);
      add_option(sora_obj, "sora-", "audio-codec-type", is_string);
      add_option(sora_obj, "sora-", "video-bit-rate", is_number);
      add_option(sora_obj, "sora-", "audio-bit-rate", is_number);
      add_option(sora_obj, "sora-", "simulcast", is_bool);
      add_option(sora_obj, "sora-", "simulcast-rid", is_string);
      add_option(sora_obj, "sora-", "spotlight", is_bool);
      add_option(sora_obj, "sora-", "spotlight-number", is_number);
      add_option(sora_obj, "sora-", "spotlight-focus-rid", is_string);
      add_option(sora_obj, "sora-", "spotlight-unfocus-rid", is_string);
      add_option(sora_obj, "sora-", "data-channel-signaling", is_bool);
      add_option(sora_obj, "sora-", "data-channel-signaling-timeout",
                 is_number);
      add_option(sora_obj, "sora-", "ignore-disconnect-websocket", is_bool);
      add_option(sora_obj, "sora-", "disconnect-wait-timeout", is_number);

      add_json_option(sora_obj, "sora-", "metadata");
      add_json_option(sora_obj, "sora-", "signaling-notify-metadata");
      add_json_option(sora_obj, "sora-", "data-channels");
      add_json_option(sora_obj, "sora-", "video-vp9-params");
      add_json_option(sora_obj, "sora-", "video-av1-params");
      add_json_option(sora_obj, "sora-", "video-h264-params");
      add_json_option(sora_obj, "sora-", "video-h265-params");
    }

    // 型が想定と異なる値が 1 つでもあれば設定エラーとして扱う
    if (has_error) {
      return std::nullopt;
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

std::optional<std::string> Util::LoadFileContents(
    const std::string& file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  // 読み込み中のエラーを検出する
  if (file.bad() || buffer.bad()) {
    return std::nullopt;
  }
  return buffer.str();
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
