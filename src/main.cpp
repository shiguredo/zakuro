#include <atomic>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Linux
#include <sys/resource.h>

// WebRTC
#include <rtc_base/log_sinks.h>
#include <rtc_base/string_utils.h>

#include <blend2d.h>
#include <yaml-cpp/yaml.h>

#include "fake_audio_key_trigger.h"
#include "fake_video_capturer.h"
#include "game/game_kuzushi.h"
#include "scenario_player.h"
#include "util.h"
#include "virtual_client.h"
#include "wav_reader.h"
#include "zakuro.h"
#include "zakuro_stats.h"

const size_t kDefaultMaxLogFileSize = 10 * 1024 * 1024;

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

int main(int argc, char* argv[]) {
  rlimit lim;
  if (::getrlimit(RLIMIT_NOFILE, &lim) != 0) {
    std::cerr << "getrlimit 失敗" << std::endl;
    return -1;
  }
  if (lim.rlim_cur < 1024) {
    std::cerr << "ファイルディスクリプタの数が足りません。"
                 "最低でも 1024 以上にして下さい。"
              << std::endl;
    std::cerr << "  soft=" << lim.rlim_cur << ", hard=" << lim.rlim_max
              << std::endl;
    return -1;
  }

  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }

  std::vector<ZakuroConfig> configs;

  std::string config_file;
  int log_level = webrtc::LS_NONE;
  int port = -1;
  std::string connection_id_stats_file;
  double instance_hatch_rate = 1.0;
  ZakuroConfig config;
  Util::ParseArgs(args, config_file, log_level, port, connection_id_stats_file,
                  instance_hatch_rate, config, false);

  if (config_file.empty()) {
    // 設定ファイルが無ければそのまま ZakuroConfig を利用する
    configs.push_back(config);
  } else {
    // 設定ファイルがある場合は設定ファイルから引数を構築し直して再度パースする
    YAML::Node config_node = YAML::LoadFile(config_file);
    if (!config_node["zakuro"]) {
      std::cerr << "設定ファイルのルートに zakuro キーがありません。"
                << std::endl;
      return 1;
    }
    const YAML::Node& zakuro_node = config_node["zakuro"];
    std::vector<std::string> common_args;
    common_args.clear();
    if (zakuro_node["log-level"]) {
      common_args.push_back("--log-level");
      common_args.push_back(zakuro_node["log-level"].as<std::string>());
    }
    if (zakuro_node["port"]) {
      common_args.push_back("--port");
      common_args.push_back(zakuro_node["port"].as<std::string>());
    }
    if (zakuro_node["output-file-connection-id"]) {
      common_args.push_back("--output-file-connection-id");
      common_args.push_back(
          zakuro_node["output-file-connection-id"].as<std::string>());
    }
    if (zakuro_node["instance-hatch-rate"]) {
      common_args.push_back("--instance-hatch-rate");
      common_args.push_back(
          zakuro_node["instance-hatch-rate"].as<std::string>());
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

    if (!zakuro_node["instances"]) {
      std::cerr << "zakuro の下に instances キーがありません。" << std::endl;
      return 1;
    }
    const YAML::Node& instances_node = zakuro_node["instances"];
    if (instances_node.size() == 0) {
      std::cerr << "instances の下に設定がありません。" << std::endl;
      return 1;
    }
    for (auto instance : instances_node) {
      auto argss = Util::NodeToArgs(instance);
      for (auto args : argss) {
        args.insert(args.begin(), common_args.begin(), common_args.end());
        args.insert(args.end(), post_args.begin(), post_args.end());

        std::cout << argv[0];
        for (auto arg : args) {
          std::cout << " " << escape_if_needed(arg);
        }
        std::cout << std::endl;

        config_file = "";
        config = ZakuroConfig();
        Util::ParseArgs(args, config_file, log_level, port,
                        connection_id_stats_file, instance_hatch_rate, config,
                        true);
        configs.push_back(config);
      }
    }
  }

  webrtc::LogMessage::LogToDebug((webrtc::LoggingSeverity)log_level);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  std::unique_ptr<webrtc::FileRotatingLogSink> log_sink(
      new webrtc::FileRotatingLogSink("./", "webrtc_logs", kDefaultMaxLogFileSize,
                                   10));
  if (!log_sink->Init()) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "Failed to open log file";
    log_sink.reset();
    return 1;
  }
  webrtc::LogMessage::AddLogToStream(log_sink.get(), webrtc::LS_INFO);

  // TODO: サーバの起動については別途考える
  //if (config_.sora_port >= 0) {
  //  SoraServerConfig config;
  //  const boost::asio::ip::tcp::endpoint endpoint{
  //      boost::asio::ip::make_address("127.0.0.1"),
  //      static_cast<unsigned short>(config_.sora_port)};
  //  // TODO: vcs をスレッドセーフにする（VC 生成スレッドと競合するので）
  //  SoraServer::Create(ioc, endpoint, &vcs, std::move(config))->Run();
  //}

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
        // ファイルに書き込む
        auto m = stats->Get();
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
        // ファイルに出力
        std::ofstream ofs(connection_id_stats_file);
        ofs << jstr;
      }
    }));
  }

  std::vector<std::unique_ptr<std::thread>> ths;
  for (int i = 0; i < configs.size(); i++) {
    const auto& config = configs[i];
    ths.push_back(std::unique_ptr<std::thread>(
        new std::thread([i, config, &stats_cv, &stats_mut, &stats_countdown,
                         instance_hatch_rate]() {
          int wait_ms = (int)(1000 * i / instance_hatch_rate);
          std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
          Zakuro zakuro(config);
          zakuro.Run();
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

  return 0;
}
