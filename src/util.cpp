#include "util.h"

#include <regex>

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

#include "zakuro_version.h"

void Util::ParseArgs(int argc, char* argv[], int& log_level, ZakuroArgs& args) {
  CLI::App app("Zakuro - WebRTC Load Testing Tool");
  app.set_help_all_flag("--help-all",
                        "Print help message for all modes and exit");

  bool version = false;
  bool video_codecs = false;

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

  app.add_option("--vcs", args.vcs, "Virtual Clients")
      ->check(CLI::Range(1, 100));
  app.add_option("--hatch-rate", args.hatch_rate,
                 "Spawned virtual clients per seconds")
      ->check(CLI::Range(0.1, 100.0));

  app.add_flag("--no-video-device", args.no_video_device,
               "Do not use video device");
  app.add_flag("--no-audio-device", args.no_audio_device,
               "Do not use audio device");
  app.add_flag("--fake-capture-device", args.fake_capture_device,
               "Fake Capture Device");
  app.add_option("--fake-video-capture", args.fake_video_capture,
                 "Fake Video from File")
      ->check(CLI::ExistingFile);
  app.add_option("--fake-audio-capture", args.fake_audio_capture,
                 "Fake Audio from File")
      ->check(CLI::ExistingFile);
  app.add_flag("--sandstorm", args.sandstorm, "Fake Sandstorm Video");
#if defined(__APPLE__)
  app.add_option("--video-device", args.video_device,
                 "Use the video device specified by an index or a name "
                 "(use the first one if not specified)");
#elif defined(__linux__)
  app.add_option("--video-device", args.video_device,
                 "Use the video input device specified by a name "
                 "(some device will be used if not specified)")
      ->check(CLI::ExistingFile);
#endif
  app.add_option("--resolution", args.resolution,
                 "Video resolution (one of QVGA, VGA, HD, FHD, 4K, or "
                 "[WIDTH]x[HEIGHT])")
      ->check(is_valid_resolution);
  app.add_option("--framerate", args.framerate, "Video framerate")
      ->check(CLI::Range(1, 60));
  app.add_flag("--fixed-resolution", args.fixed_resolution,
               "Maintain video resolution in degradation");
  app.add_set("--priority", args.priority,
              {"BALANCE", "FRAMERATE", "RESOLUTION"},
              "Preference in video degradation (experimental)");
  app.add_flag("--version", version, "Show version information");
  app.add_flag("--insecure", args.insecure,
               "Allow insecure server connections when using SSL");
  auto log_level_map = std::vector<std::pair<std::string, int> >(
      {{"verbose", 0}, {"info", 1}, {"warning", 2}, {"error", 3}, {"none", 4}});
  app.add_option("--log-level", log_level, "Log severity level threshold")
      ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case));
  app.add_option("--openh264", args.openh264,
                 "OpenH264 dynamic library path. \"OpenH264 Video Codec "
                 "provided by Cisco Systems, Inc.\"")
      ->check(CLI::ExistingFile);
  app.add_set("--game", args.game, {"kuzushi"}, "Play game");

  app.add_option("SIGNALING-URL", args.sora_signaling_url, "Signaling URL");
  app.add_option("--channel-id", args.sora_channel_id, "Channel ID");
  app.add_flag("--auto", args.sora_auto_connect,
               "Connect to Sora automatically");

  auto bool_map = std::vector<std::pair<std::string, bool> >(
      {{"false", false}, {"true", true}});
  app.add_option("--video", args.sora_video,
                 "Send video to sora (default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--audio", args.sora_audio,
                 "Send audio to sora (default: true)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_set("--video-codec-type", args.sora_video_codec_type,
              {"", "VP8", "VP9", "AV1", "H264"}, "Video codec for send");
  app.add_set("--audio-codec-type", args.sora_audio_codec_type, {"", "OPUS"},
              "Audio codec for send");
  app.add_option("--video-bit-rate", args.sora_video_bit_rate, "Video bit rate")
      ->check(CLI::Range(0, 30000));
  app.add_option("--audio-bit-rate", args.sora_audio_bit_rate, "Audio bit rate")
      ->check(CLI::Range(0, 510));
  app.add_set("--role", args.sora_role, {"sendonly", "recvonly", "sendrecv"},
              "Role");
  app.add_option("--multistream", args.sora_multistream,
                 "Use multistream (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--simulcast", args.sora_simulcast,
                 "Use simulcast (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--spotlight", args.sora_spotlight,
                 "Use spotlight (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--spotlight-number", args.sora_spotlight_number,
                 "Number of spotlight")
      ->check(CLI::Range(0, 8));
  app.add_option("--port", args.sora_port, "Port number (default: -1)")
      ->check(CLI::Range(-1, 65535));

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
  app.add_option("--metadata", sora_metadata,
                 "Signaling metadata used in connect message")
      ->check(is_json);
  std::string sora_signaling_notify_metadata;
  app.add_option("--signaling-notify-metadata", sora_signaling_notify_metadata,
                 "Signaling metadata")
      ->check(is_json);

  try {
    app.parse(argc, argv);
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

  if (args.sora_signaling_url.empty()) {
    std::cerr << "SIGNALING-URL is required" << std::endl;
    exit(1);
  }

  if (args.sora_channel_id.empty()) {
    std::cerr << "--channel-id is required" << std::endl;
    exit(1);
  }

  if (args.sora_role.empty()) {
    std::cerr << "--role is required" << std::endl;
    exit(1);
  }

  // サイマルキャストは VP8 か H264 のみで動作する
  if (args.sora_simulcast && args.sora_video_codec_type != "VP8" &&
      args.sora_video_codec_type != "H264") {
    std::cerr << "Simulcast works only --video-codec=VP8 or H264." << std::endl;
    exit(1);
  }

  // H264 は --openh264 が指定されてる場合のみ動作する
  if (args.sora_video_codec_type == "H264" && args.openh264.empty()) {
    std::cerr << "Specify --openh264=/path/to/libopenh264.so for H.264 codec"
              << std::endl;
    exit(1);
  }
  // --openh264 のパスは絶対パスである必要がある
  if (!args.openh264.empty() && args.openh264[0] != '/') {
    std::cerr << "--openh264 file path must be absolute path" << std::endl;
    exit(1);
  }

  // メタデータのパース
  if (!sora_metadata.empty()) {
    args.sora_metadata = boost::json::parse(sora_metadata);
  }
  if (!sora_signaling_notify_metadata.empty()) {
    args.sora_signaling_notify_metadata =
        boost::json::parse(sora_signaling_notify_metadata);
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
