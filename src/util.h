#ifndef UTIL_H_
#define UTIL_H_

#include <optional>
#include <string>
#include <vector>

// Boost
#include <boost/json.hpp>

// WebRTC
#include <api/peer_connection_interface.h>

#include "zakuro.h"

// Util::ParseArgs の戻り値
// 終了コードを持つため、int だけでは「0 で終了」と「続行」を区別できない
struct ParseArgsResult {
  enum class Code {
    // 続行する
    Continue,
    // 何も起動せず正常終了する (--version / --show-video-codec-capability)
    ExitSuccess,
    // エラー終了する。exit_code を終了コードに使う
    ErrorExit,
  };
  Code code = Code::Continue;
  // ErrorExit のときの終了コード。CLI11 が返す値 (検証エラーは 105) や 1 を入れる
  int exit_code = 0;

  static ParseArgsResult Continue() { return {Code::Continue, 0}; }
  static ParseArgsResult ExitSuccess() { return {Code::ExitSuccess, 0}; }
  static ParseArgsResult ErrorExit(int exit_code) {
    return {Code::ErrorExit, exit_code};
  }
};

class Util {
 public:
  // 引数を解析して config などへ設定する
  // 解析の結果、続行するか・終了するかを戻り値で返す
  static ParseArgsResult ParseArgs(const std::vector<std::string>& args,
                                   std::string& config_file,
                                   int& log_level,
                                   std::optional<std::string>& http_host,
                                   std::optional<int>& http_port,
                                   std::string& connection_id_stats_file,
                                   double& instance_hatch_rate,
                                   ZakuroConfig& config,
                                   bool ignore_config);
  // JSONC の instance オブジェクトを CLI 引数の配列に変換する
  // instance がオブジェクトでない場合、必要なキーの型が想定と異なる場合、
  // 値が範囲外の場合は std::nullopt を返す (エラーは std::cerr に英語で出力する)
  static std::optional<std::vector<std::vector<std::string>>>
  ParseInstanceToArgs(const boost::json::value& inst);
  // JSONC ファイルを読み込む
  // 拡張子が .json / .jsonc でない場合、ファイルを開けない場合、
  // JSON としてパースできない場合は std::runtime_error を投げる
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
