#include "util.h"

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
#include <rtc_base/helpers.h>

#include "zakuro.h"
#include "zakuro_version.h"

namespace std {

std::string to_string(std::string str) {
  return str;
}

}  // namespace std

void Util::ParseArgs(std::vector<std::string>& args,
                     std::string& config_file,
                     int& log_level,
                     int& port,
                     ZakuroConfig& config) {
  std::reverse(args.begin(), args.end());

  CLI::App app("Zakuro - WebRTC Load Testing Tool");

  // アプリケーション全体で１個しか存在しない共通オプション
  bool version = false;
  app.add_flag("--version", version, "Show version information");

  app.add_option("--config", config_file, "YAML config file path")
      ->check(CLI::ExistingFile);

  auto log_level_map = std::vector<std::pair<std::string, int> >(
      {{"verbose", 0}, {"info", 1}, {"warning", 2}, {"error", 3}, {"none", 4}});
  app.add_option("--log-level", log_level, "Log severity level threshold")
      ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case));
  app.add_option("--port", port, "Port number (default: -1)")
      ->check(CLI::Range(-1, 65535));

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

  app.add_option("--name", config.name, "Client Name");
  app.add_option("--vcs", config.vcs, "Virtual Clients")
      ->check(CLI::Range(1, 100));
  app.add_option("--hatch-rate", config.hatch_rate,
                 "Spawned virtual clients per seconds")
      ->check(CLI::Range(0.1, 100.0));

  app.add_flag("--no-video-device", config.no_video_device,
               "Do not use video device");
  app.add_flag("--no-audio-device", config.no_audio_device,
               "Do not use audio device");
  app.add_flag("--fake-capture-device", config.fake_capture_device,
               "Fake Capture Device");
  app.add_option("--fake-video-capture", config.fake_video_capture,
                 "Fake Video from File")
      ->check(CLI::ExistingFile);
  app.add_option("--fake-audio-capture", config.fake_audio_capture,
                 "Fake Audio from File")
      ->check(CLI::ExistingFile);
  app.add_flag("--sandstorm", config.sandstorm, "Fake Sandstorm Video");
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
                 "[WIDTH]x[HEIGHT])")
      ->check(is_valid_resolution);
  app.add_option("--framerate", config.framerate, "Video framerate")
      ->check(CLI::Range(1, 60));
  app.add_flag("--fixed-resolution", config.fixed_resolution,
               "Maintain video resolution in degradation");
  app.add_set("--priority", config.priority,
              {"BALANCE", "FRAMERATE", "RESOLUTION"},
              "Preference in video degradation (experimental)");
  app.add_flag("--insecure", config.insecure,
               "Allow insecure server connections when using SSL");
  app.add_option("--openh264", config.openh264,
                 "OpenH264 dynamic library path. \"OpenH264 Video Codec "
                 "provided by Cisco Systems, Inc.\"")
      ->check(CLI::ExistingFile);
  app.add_set("--game", config.game, {"kuzushi"}, "Play game");
  app.add_set("--scenario", config.scenario, {"", "reconnect"},
              "Scenario type");

  // Sora 系オプション
  app.add_option("--sora-signaling-url", config.sora_signaling_url,
                 "Signaling URL");
  app.add_option("--sora-channel-id", config.sora_channel_id, "Channel ID");
  app.add_set("--sora-role", config.sora_role,
              {"sendonly", "recvonly", "sendrecv"}, "Role");

  auto bool_map = std::vector<std::pair<std::string, bool> >(
      {{"false", false}, {"true", true}});
  app.add_option("--sora-video", config.sora_video,
                 "Send video to sora (default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-audio", config.sora_audio,
                 "Send audio to sora (default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_set("--sora-video-codec-type", config.sora_video_codec_type,
              {"", "VP8", "VP9", "AV1", "H264"}, "Video codec for send");
  app.add_set("--sora-audio-codec-type", config.sora_audio_codec_type,
              {"", "OPUS"}, "Audio codec for send");
  app.add_option("--sora-video-bit-rate", config.sora_video_bit_rate,
                 "Video bit rate")
      ->check(CLI::Range(0, 30000));
  app.add_option("--sora-audio-bit-rate", config.sora_audio_bit_rate,
                 "Audio bit rate")
      ->check(CLI::Range(0, 510));
  app.add_option("--sora-multistream", config.sora_multistream,
                 "Use multistream (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-simulcast", config.sora_simulcast,
                 "Use simulcast (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-spotlight", config.sora_spotlight,
                 "Use spotlight (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-spotlight-number", config.sora_spotlight_number,
                 "Number of spotlight")
      ->check(CLI::Range(0, 8));
  app.add_option("--sora-data-channel-signaling",
                 config.sora_data_channel_signaling,
                 "Use DataChannel for Sora signaling (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-data-channel-signaling-timeout",
                 config.sora_data_channel_signaling_timeout,
                 "Timeout for Data Channel in seconds (default: 30)")
      ->check(CLI::PositiveNumber);
  app.add_option("--sora-ignore-disconnect-websocket",
                 config.sora_ignore_disconnect_websocket,
                 "Ignore WebSocket disconnection if using Data Channel "
                 "(default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--sora-close-websocket", config.sora_close_websocket,
                 "Close WebSocket when starting to use Data Channel "
                 "(only if --sora-ignore-disconnect-websocket=true) "
                 "(default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));

  auto is_json = CLI::Validator(
      [](std::string input) -> std::string {
        boost::json::error_code ec;
        boost::json::parse(input, ec);
        if (ec) {
          return "Value " + input + " is not JSON Value";
        }
        return std::string();
      },
      "JSON Value");
  std::string sora_metadata;
  app.add_option("--sora-metadata", sora_metadata,
                 "Signaling metadata used in connect message")
      ->check(is_json);
  std::string sora_signaling_notify_metadata;
  app.add_option("--sora-signaling-notify-metadata",
                 sora_signaling_notify_metadata, "Signaling metadata")
      ->check(is_json);

  try {
    app.parse(args);
  } catch (const CLI::ParseError& e) {
    exit(app.exit(e));
  }

  if (version) {
    std::cout << ZakuroVersion::GetClientName() << std::endl;
    std::cout << std::endl;
    std::cout << "WebRTC: " << ZakuroVersion::GetLibwebrtcName() << std::endl;
    std::cout << "Environment: " << ZakuroVersion::GetEnvironmentName()
              << std::endl;
    exit(0);
  }

  // 設定ファイルがある
  if (!config_file.empty()) {
    return;
  }

  // 必須オプション。
  // add_option()->required() を使うと --version や --config を指定した際に
  // エラーになってしまうので、ここでチェックする
  if (config.sora_signaling_url.empty()) {
    std::cerr << "--sora-signaling-url is required" << std::endl;
    exit(1);
  }
  if (config.sora_channel_id.empty()) {
    std::cerr << "--sora-channel-id is required" << std::endl;
    exit(1);
  }
  if (config.sora_role.empty()) {
    std::cerr << "--sora-role is required" << std::endl;
    exit(1);
  }

  // サイマルキャストは VP8 か H264 のみで動作する
  if (config.sora_simulcast && config.sora_video_codec_type != "VP8" &&
      config.sora_video_codec_type != "H264") {
    std::cerr << "Simulcast works only --sora-video-codec=VP8 or H264."
              << std::endl;
    exit(1);
  }

  // H264 は --openh264 が指定されてる場合のみ動作する
  if (config.sora_video_codec_type == "H264" && config.openh264.empty()) {
    std::cerr << "Specify --openh264=/path/to/libopenh264.so for H.264 codec"
              << std::endl;
    exit(1);
  }
  // --openh264 のパスは絶対パスである必要がある
  if (!config.openh264.empty() && config.openh264[0] != '/') {
    std::cerr << "--openh264 file path must be absolute path" << std::endl;
    exit(1);
  }

  // メタデータのパース
  if (!sora_metadata.empty()) {
    config.sora_metadata = boost::json::parse(sora_metadata);
  }
  if (!sora_signaling_notify_metadata.empty()) {
    config.sora_signaling_notify_metadata =
        boost::json::parse(sora_signaling_notify_metadata);
  }
}

std::vector<std::string> Util::NodeToArgs(const YAML::Node& inst) {
  std::vector<std::string> args;

  bool has_error = false;

#define DEF_SCALAR(x, prefix, key, type, type_name)                                       \
  try {                                                                                   \
    const YAML::Node& node = x[key];                                                      \
    if (node) {                                                                           \
      if (!node.IsScalar()) {                                                             \
        std::cerr << "\"" key "\" の値は " type_name " である必要があります。" \
                  << std::endl;                                                           \
        has_error = true;                                                                 \
      } else {                                                                            \
        args.push_back("--" prefix key);                                                  \
        args.push_back(std::to_string(node.as<type>()));                                  \
      }                                                                                   \
    }                                                                                     \
  } catch (YAML::BadConversion & e) {                                                     \
    std::cerr << "\"" key "\" の値は " type_name " である必要があります。"     \
              << std::endl;                                                               \
    has_error = true;                                                                     \
  }

#define DEF_STRING(x, prefix, key) \
  DEF_SCALAR(x, prefix, key, std::string, "文字列")
#define DEF_INTEGER(x, prefix, key) DEF_SCALAR(x, prefix, key, int, "整数")
#define DEF_DOUBLE(x, prefix, key) DEF_SCALAR(x, prefix, key, double, "実数")
#define DEF_BOOLEAN(x, prefix, key) DEF_SCALAR(x, prefix, key, bool, "bool値")

#define DEF_FLAG(x, prefix, key)                                                       \
  try {                                                                                \
    const YAML::Node& node = x[key];                                                   \
    if (node) {                                                                        \
      if (!node.IsScalar()) {                                                          \
        std::cerr << "\"" key "\" の値は bool値 である必要があります。" \
                  << std::endl;                                                        \
        has_error = true;                                                              \
      } else {                                                                         \
        if (node.as<bool>()) {                                                         \
          args.push_back("--" prefix key);                                             \
        }                                                                              \
      }                                                                                \
    }                                                                                  \
  } catch (YAML::BadConversion & e) {                                                  \
    std::cerr << "\"" key "\" の値は bool値 である必要があります。"     \
              << std::endl;                                                            \
    has_error = true;                                                                  \
  }

  DEF_STRING(inst, "", "name");
  DEF_INTEGER(inst, "", "vcs");
  DEF_DOUBLE(inst, "", "hatch-rate");
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

  const YAML::Node& sora = inst["sora"];
  if (sora) {
    DEF_STRING(sora, "sora-", "signaling-url");
    DEF_STRING(sora, "sora-", "channel-id");
    DEF_STRING(sora, "sora-", "role");
    DEF_BOOLEAN(sora, "sora-", "video");
    DEF_BOOLEAN(sora, "sora-", "audio");
    DEF_STRING(sora, "sora-", "video-codec-type");
    DEF_STRING(sora, "sora-", "audio-codec-type");
    DEF_INTEGER(sora, "sora-", "video-bit-rate");
    DEF_INTEGER(sora, "sora-", "audio-bit-rate");
    DEF_BOOLEAN(sora, "sora-", "multistream");
    DEF_BOOLEAN(sora, "sora-", "simulcast");
    DEF_BOOLEAN(sora, "sora-", "spotlight");
    DEF_INTEGER(sora, "sora-", "spotlight-number");
    DEF_BOOLEAN(sora, "sora-", "data-channel-signaling");
    DEF_INTEGER(sora, "sora-", "data-channel-signaling-timeout");
    DEF_BOOLEAN(sora, "sora-", "ignore-disconnect-websocket");
    DEF_BOOLEAN(sora, "sora-", "close-websocket");
    if (sora["metadata"]) {
      boost::json::value value = NodeToJson(sora["metadata"]);
      args.push_back("--sora-metadata");
      args.push_back(boost::json::serialize(value));
    }
    if (sora["signaling-notify-metadata"]) {
      boost::json::value value = NodeToJson(sora["signaling-notify-metadata"]);
      args.push_back("--sora-signaling-notify-metadata");
      args.push_back(boost::json::serialize(value));
    }
  }

  if (has_error) {
    throw std::exception();
  }

  return args;
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
  rtc::CreateRandomString(length, &result);
  return result;
}

std::string Util::GenerateRandomNumericChars(size_t length) {
  auto random_numerics = []() -> char {
    const char charset[] = "0123456789";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
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
  res.body() = why.to_string();
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
  res.body() = "The resource '" + target.to_string() + "' was not found.";
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
  res.body() = "An error occurred: '" + what.to_string() + "'";
  res.prepare_payload();
  return res;
}
