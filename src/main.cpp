#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// Boost
#include <boost/json.hpp>

// POSIX
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

// WebRTC
#include <rtc_base/log_sinks.h>
#include <rtc_base/string_utils.h>

#include <blend2d/blend2d.h>

#include "fake_audio_key_trigger.h"
#include "fake_video_capturer.h"
#include "http_server.h"
#include "scenario_player.h"
#include "util.h"
#include "virtual_client.h"
#include "wav_reader.h"
#include "zakuro.h"
#include "zakuro_stats.h"

const size_t kDefaultMaxLogFileSize = 10 * 1024 * 1024;

namespace {

// AddLogToStream で登録したシンクを、破棄する前に RemoveLogToStream する。
// 解除せずに破棄すると、LogMessage の静的リストが破棄済みシンクを指したまま残る。
// main の return 後に RTC_LOG が走ると、そのシンクへ書き込む。
class InstalledFileLogSink {
 public:
  // sink は Init() に成功した非 null である前提。null は受け取らない。
  explicit InstalledFileLogSink(
      std::unique_ptr<webrtc::FileRotatingLogSink> sink)
      : sink_(std::move(sink)) {
    assert(sink_ != nullptr);
    webrtc::LogMessage::AddLogToStream(sink_.get(), webrtc::LS_INFO);
  }

  ~InstalledFileLogSink() {
    webrtc::LogMessage::RemoveLogToStream(sink_.get());
  }

  InstalledFileLogSink(const InstalledFileLogSink&) = delete;
  InstalledFileLogSink& operator=(const InstalledFileLogSink&) = delete;

 private:
  std::unique_ptr<webrtc::FileRotatingLogSink> sink_;
};

}  // namespace

// 雑なエスケープ処理
// 文字列中に \ や " が含まれてたら全体をエスケープする
std::string escape_if_needed(std::string str) {
  auto n = str.find_first_of("\\\"");
  if (n == std::string::npos) {
    return str;
  }
  std::string s;
  s += '\"';
  for (auto c : str) {
    switch (c) {
      case '\\':
      case '\"':
        s += '\\';
    }
    s += c;
  }
  s += '\"';
  return s;
}

// 1 VC あたりに必要と見積もるファイルディスクリプタ数
// WebSocket 用の 1 と ICE / DTLS 用の複数を保守的に見積もった値
const rlim_t kFileDescriptorsPerVirtualClient = 5;

// ファイルディスクリプタの必要数が足りないことを伝えるメッセージ
// 他のスレッドが同じ stderr へ書き込んでも混ざらないよう、1 回の出力で出す
static std::string FileDescriptorLimitMessage(const std::string& reason,
                                              rlim_t required,
                                              rlim_t soft,
                                              rlim_t hard) {
  std::ostringstream oss;
  oss << reason << ": required=" << required << " soft=" << soft
      << " hard=" << hard;
  return oss.str();
}

// 必要 FD 数を満たすように soft limit を昇格する
// 昇格しても足りない場合は false を返す
static bool EnsureFileDescriptorLimit(rlim_t required) {
  rlimit lim;
  if (::getrlimit(RLIMIT_NOFILE, &lim) != 0) {
    std::cerr << "failed to get the file descriptor limit" << std::endl;
    return false;
  }
  if (lim.rlim_cur >= required) {
    // 実際に使える soft limit を残す。FD が足りずに接続が失敗したときの切り分けに使う
    RTC_LOG(LS_INFO) << "file descriptor limit: required=" << required
                     << " soft=" << lim.rlim_cur << " hard=" << lim.rlim_max;
    return true;
  }

  // soft limit は hard limit を超えられないため、hard limit で頭打ちにして引き上げる
  rlimit next = lim;
  next.rlim_cur = std::min(required, lim.rlim_max);
  if (::setrlimit(RLIMIT_NOFILE, &next) != 0) {
    std::cerr << FileDescriptorLimitMessage(
                     "failed to raise the file descriptor limit", required,
                     lim.rlim_cur, lim.rlim_max)
              << std::endl;
    return false;
  }

  // soft limit は hard limit までしか上げられないため、hard limit で足りるかを判定する
  if (lim.rlim_max < required) {
    // soft limit は hard limit まで引き上げ済みなので、その値を出す
    std::cerr << FileDescriptorLimitMessage(
                     "the file descriptor limit is too low", required,
                     next.rlim_cur, lim.rlim_max)
              << std::endl;
    return false;
  }

  // 昇格後の値を残す。昇格が効いているかをテストからも確認できる
  RTC_LOG(LS_INFO) << "raised the file descriptor limit: required=" << required
                   << " soft=" << next.rlim_cur << " hard=" << lim.rlim_max;
  return true;
}

// 接続 ID の stats をファイルへ書き出す
// 外部から読まれる途中の状態を見せないため、同じディレクトリのテンポラリファイルへ
// 書き切ってから std::filesystem::rename で置き換える
// (同一ディレクトリなので POSIX の rename(2) になりアトミックに置き換わる)
static void WriteStatsFile(const std::string& path, const std::string& json) {
  std::filesystem::path target(path);
  std::filesystem::path temp =
      target.parent_path() /
      (target.filename().string() + ".tmp." + std::to_string(::getpid()));

  // 不完全なテンポラリファイルを残さないための後始末
  auto remove_temp = [&temp]() {
    std::error_code remove_ec;
    std::filesystem::remove(temp, remove_ec);
  };

  // 0600 で作り、出力先のパーミッションが分かった時点で合わせる。
  // std::ofstream は umask に依存したモードで作るため、内容が書かれた緩い
  // パーミッションのファイルが一瞬できる。ここでは内容を書く前にモードを確定させる
  int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    RTC_LOG(LS_ERROR) << "Failed to open the stats file: " << temp.string();
    return;
  }

  // 出力先が既にある場合はそのパーミッションを引き継ぐ
  // まだ無い場合 (初回の書き出し) は 0600 のままにする
  struct stat target_stat;
  if (::stat(target.c_str(), &target_stat) == 0) {
    if (::fchmod(fd, target_stat.st_mode & 07777) != 0) {
      RTC_LOG(LS_WARNING) << "Failed to copy the permission of the stats file: "
                          << target.string() << ": " << std::strerror(errno);
    }
  }

  size_t written = 0;
  while (written < json.size()) {
    ssize_t n = ::write(fd, json.data() + written, json.size() - written);
    if (n < 0) {
      RTC_LOG(LS_ERROR) << "Failed to write the stats file: " << temp.string()
                        << ": " << std::strerror(errno);
      ::close(fd);
      remove_temp();
      return;
    }
    written += (size_t)n;
  }
  // close の失敗 (書き込みの失敗を含む) を検査してから置き換える
  if (::close(fd) != 0) {
    RTC_LOG(LS_ERROR) << "Failed to write the stats file: " << temp.string()
                      << ": " << std::strerror(errno);
    remove_temp();
    return;
  }

  std::error_code rename_ec;
  std::filesystem::rename(temp, target, rename_ec);
  if (rename_ec) {
    RTC_LOG(LS_ERROR) << "Failed to replace the stats file: " << path << ": "
                      << rename_ec.message();
    remove_temp();
    return;
  }
}

int main(int argc, char* argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }

  std::vector<ZakuroConfig> configs;

  std::string config_file;
  int log_level = webrtc::LS_NONE;
  std::optional<std::string> http_host;
  std::optional<int> http_port;
  std::string connection_id_stats_file;
  double instance_hatch_rate = 1.0;
  ZakuroConfig config;
  auto parse_result = Util::ParseArgs(args, config_file, log_level, http_host,
                                      http_port, connection_id_stats_file,
                                      instance_hatch_rate, config, false);
  if (parse_result.code == ParseArgsResult::Code::ExitSuccess) {
    return 0;
  }
  if (parse_result.code == ParseArgsResult::Code::ErrorExit) {
    return parse_result.exit_code;
  }

  if (config_file.empty()) {
    // 設定ファイルが無ければそのまま ZakuroConfig を利用する
    configs.push_back(config);
  } else {
    // 設定ファイルがある場合は設定ファイルから引数を構築し直して再度パースする
    boost::json::value zakuro_value;
    try {
      zakuro_value = Util::LoadJsoncFile(config_file);
    } catch (const std::exception& e) {
      // 拡張子不正・ファイルオープン失敗・JSON パース失敗はここに来る
      std::cerr << "failed to load config file: " << e.what() << std::endl;
      return 1;
    }

    // 設定ファイルのルートと instances は型が確定していないので先に型を検査する
    if (!zakuro_value.is_object()) {
      std::cerr << "config file must be a JSON object" << std::endl;
      return 1;
    }
    const auto& zakuro_obj = zakuro_value.as_object();

    std::vector<std::string> common_args;
    common_args.clear();

    // トップレベルの共通オプションの値を CLI 引数へ変換する。
    // 値が空文字列に潰れると CLI11 の検証をすり抜けて無言で通るものがあるため、
    // オブジェクトと配列は設定ミスとして扱う
    for (const auto& key :
         {"log-level", "http-port", "http-host", "output-file-connection-id",
          "instance-hatch-rate"}) {
      auto it = zakuro_obj.find(key);
      if (it == zakuro_obj.end()) {
        continue;
      }
      if (it->value().is_object() || it->value().is_array()) {
        // null は CLI 引数にすると "null" という文字列になり設定ミスに気付けないため、
        // ここで型として説明せず、実際に受け付ける型だけを挙げる
        std::cerr << key << " must be a string, a number, or a boolean"
                  << std::endl;
        return 1;
      }
      common_args.push_back("--" + std::string(key));
      common_args.push_back(Util::PrimitiveValueToString(it->value()));
    }

    std::vector<std::string> post_args;
    // args の --config を取り除きつつ post_args に追加
    for (auto it = args.begin(); it != args.end(); ++it) {
      if (*it == "--config") {
        // --config hoge
        ++it;
        continue;
      }
      if (it->find("--config=") == 0) {
        continue;
      }
      post_args.push_back(*it);
    }

    auto instances_it = zakuro_obj.find("instances");
    if (instances_it == zakuro_obj.end()) {
      std::cerr << "instances キーがありません。" << std::endl;
      return 1;
    }
    if (!instances_it->value().is_array()) {
      std::cerr << "instances must be an array" << std::endl;
      return 1;
    }
    const auto& instances_array = instances_it->value().as_array();
    if (instances_array.size() == 0) {
      std::cerr << "instances の下に設定がありません。" << std::endl;
      return 1;
    }
    for (const auto& instance : instances_array) {
      auto argss = Util::ParseInstanceToArgs(instance);
      if (!argss) {
        std::cerr << "failed to parse instance settings" << std::endl;
        return 1;
      }
      for (auto& args : *argss) {
        args.insert(args.begin(), common_args.begin(), common_args.end());
        args.insert(args.end(), post_args.begin(), post_args.end());

        std::cout << argv[0];
        for (auto arg : args) {
          std::cout << " " << escape_if_needed(arg);
        }
        std::cout << std::endl;

        config_file = "";
        config = ZakuroConfig();
        auto instance_result = Util::ParseArgs(
            args, config_file, log_level, http_host, http_port,
            connection_id_stats_file, instance_hatch_rate, config, true);
        if (instance_result.code == ParseArgsResult::Code::ExitSuccess) {
          return 0;
        }
        if (instance_result.code == ParseArgsResult::Code::ErrorExit) {
          return instance_result.exit_code;
        }
        configs.push_back(config);
      }
    }
  }

  webrtc::LogMessage::LogToDebug((webrtc::LoggingSeverity)log_level);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  std::unique_ptr<webrtc::FileRotatingLogSink> log_sink(
      new webrtc::FileRotatingLogSink("./", "webrtc_logs",
                                      kDefaultMaxLogFileSize, 10));
  if (!log_sink->Init()) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "Failed to open log file";
    log_sink.reset();
    return 1;
  }
  // 以降の return では、デストラクタが RemoveLogToStream してからシンクを破棄する。
  [[maybe_unused]] InstalledFileLogSink installed_log_sink(std::move(log_sink));

  std::shared_ptr<GameKeyCore> key_core(new GameKeyCore());
  key_core->Init();
  // 各 config に GameKeyCore の設定を入れていく
  for (auto& config : configs) {
    config.key_core = key_core;
  }

  // 各 config に stats を設定
  std::shared_ptr<ZakuroStats> stats(new ZakuroStats());
  for (auto& config : configs) {
    config.stats = stats;
  }

  // ユニークな番号を設定
  for (int i = 0; i < configs.size(); i++) {
    configs[i].id = i;
  }

  // ファイルディスクリプタの必要数を確認する
  // インスタンスごとに --vcs を持つため、全インスタンスの合計で見積もる
  rlim_t required_fds = 0;
  for (const auto& config : configs) {
    required_fds += (rlim_t)config.vcs * kFileDescriptorsPerVirtualClient;
  }
  if (!EnsureFileDescriptorLimit(required_fds)) {
    return 1;
  }

  // HTTP サーバーの起動
  std::unique_ptr<HttpServer> http_server;
  if (http_host && http_port) {
    http_server.reset(new HttpServer(*http_host, *http_port));
    http_server->Start();
    RTC_LOG(LS_INFO) << "HTTP server started on " << *http_host << ":"
                     << *http_port;
  } else if (http_host || http_port) {
    std::cerr << "--http-host と --http-port は両方指定する必要があります"
              << std::endl;
    return 1;
  }

  // 集めた stats を定期的にファイルに出力する
  std::unique_ptr<std::thread> stats_th;
  // C++20 にしないと latch が無いので mutex+CV で終了を検知する
  std::mutex stats_mut;
  std::condition_variable stats_cv;
  int stats_countdown = configs.size();
  if (!connection_id_stats_file.empty()) {
    stats_th.reset(new std::thread([stats, &stats_cv, &stats_mut,
                                    &stats_countdown,
                                    &connection_id_stats_file]() {
      while (true) {
        std::unique_lock<std::mutex> lock(stats_mut);
        bool countzero = stats_cv.wait_for(
            lock, std::chrono::seconds(10),
            [&stats_countdown]() { return stats_countdown == 0; });
        // stats_countdown == 0 になったので終了
        if (countzero) {
          break;
        }
        /*
        {
          "wss://hoge1.jp/signaling": {
            "channelid-1": [
              "connectionid-1",
              "connectionid-2"
            ],
            "channelid-2": [
              "connectionid-3"
            ]
          },
          "wss://hoge2.jp/signaling": {
            "channelid-1": [
              "connectionid-4"
            ]
          }
        }
        */
        auto m = stats->Get();
        std::map<std::string, std::map<std::string, std::vector<std::string>>>
            d;
        for (const auto& p : m) {
          for (const auto& stat : p.second.stats) {
            d[stat.connected_url][stat.channel_id].push_back(
                stat.connection_id);
          }
        }
        // 頑張って object に変換する
        boost::json::object obj;
        for (const auto& p : d) {
          boost::json::object obj2;
          for (const auto& p2 : p.second) {
            boost::json::array ar(p2.second.begin(), p2.second.end());
            obj2[p2.first] = ar;
          }
          obj[p.first] = obj2;
        }
        std::string jstr = boost::json::serialize(obj);
        // ファイルの書き出しはロックを保持したまま行わない
        lock.unlock();
        WriteStatsFile(connection_id_stats_file, jstr);
      }
    }));
  }

  // 各インスタンスの Run の戻り値を集約する
  // Run は別スレッドで動くため、複数スレッドから書いても競合しない型にする
  std::atomic<bool> has_error(false);

  std::vector<std::unique_ptr<std::thread>> ths;
  for (int i = 0; i < configs.size(); i++) {
    const auto& config = configs[i];
    ths.push_back(std::unique_ptr<std::thread>(
        new std::thread([i, config, &stats_cv, &stats_mut, &stats_countdown,
                         &has_error, instance_hatch_rate]() {
          int wait_ms = (int)(1000 * i / instance_hatch_rate);
          std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
          Zakuro zakuro(config);
          if (zakuro.Run() != 0) {
            has_error = true;
          }
          std::lock_guard<std::mutex> guard(stats_mut);
          if (--stats_countdown == 0) {
            stats_cv.notify_all();
          }
        })));
  }
  for (auto& th : ths) {
    th->join();
  }
  if (stats_th) {
    stats_th->join();
  }

  return has_error ? 1 : 0;
}
