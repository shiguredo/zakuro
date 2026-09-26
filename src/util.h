#ifndef UTIL_H_
#define UTIL_H_

#include <optional>

// Boost
#include <boost/json.hpp>

// WebRTC
#include <api/peer_connection_interface.h>

#include "zakuro.h"

class Util {
 public:
  static void ParseArgs(const std::vector<std::string>& args,
                        std::string& config_file,
                        int& log_level,
                        std::optional<std::string>& http_host,
                        std::optional<int>& http_port,
                        bool& ui,
                        std::optional<std::string>& ui_remote_url,
                        std::string& connection_id_stats_file,
                        double& instance_hatch_rate,
                        ZakuroConfig& config,
                        bool ignore_config);
  static std::vector<std::vector<std::string>> ParseInstanceToArgs(
      const boost::json::value& inst);
  static boost::json::value LoadJsoncFile(const std::string& file_path);
  // ファイル全体を読み込む
  // ファイルを開けなかった場合や読み込みに失敗した場合は std::nullopt を返す
  // 読み込みに成功した場合はファイルの内容を返す (内容が空の場合は空文字列)
  static std::optional<std::string> LoadFileContents(
      const std::string& file_path);
  static std::string GenerateRandomChars();
  static std::string GenerateRandomChars(size_t length);
  static std::string GenerateRandomNumericChars(size_t length);
  static std::string IceConnectionStateToString(
      webrtc::PeerConnectionInterface::IceConnectionState state);
  // JSON値から文字列を取得するヘルパー関数
  static std::string PrimitiveValueToString(const boost::json::value& value);
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
