#ifndef UTIL_H_
#define UTIL_H_

// Boost
#include <boost/beast/core/string.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/json.hpp>

// WebRTC
#include <api/peer_connection_interface.h>

// YAML
#include <yaml-cpp/yaml.h>

#include "zakuro.h"

class Util {
 public:
  static void ParseArgs(const std::vector<std::string>& args,
                        std::string& config_file,
                        int& log_level,
                        int& port,
                        ZakuroConfig& config,
                        bool ignore_config);
  static std::vector<std::vector<std::string>> NodeToArgs(const YAML::Node& inst);
  static boost::json::value NodeToJson(const YAML::Node& node);
  static std::string GenerateRandomChars();
  static std::string GenerateRandomChars(size_t length);
  static std::string GenerateRandomNumericChars(size_t length);
  static std::string IceConnectionStateToString(
      webrtc::PeerConnectionInterface::IceConnectionState state);

  // MIME type をファイル名の拡張子から調べる
  static boost::beast::string_view MimeType(boost::beast::string_view path);

  // エラーレスポンスをいい感じに作る便利関数
  static boost::beast::http::response<boost::beast::http::string_body>
  BadRequest(
      const boost::beast::http::request<boost::beast::http::string_body>& req,
      boost::beast::string_view why);
  static boost::beast::http::response<boost::beast::http::string_body> NotFound(
      const boost::beast::http::request<boost::beast::http::string_body>& req,
      boost::beast::string_view target);
  static boost::beast::http::response<boost::beast::http::string_body>
  ServerError(
      const boost::beast::http::request<boost::beast::http::string_body>& req,
      boost::beast::string_view what);
};

// boost::system::error_code のエラーをいい感じに出力するマクロ
//
// if (ec)
//   return ZAKURO_BOOST_ERROR(ec, "onRead")
//
// のように、return と組み合わせて使える。
#define ZAKURO_BOOST_ERROR(ec, what)                                    \
  ([&ec] {                                                              \
    RTC_LOG(LS_ERROR) << __FUNCTION__ << " " what ": " << ec.message(); \
  }())

#endif
