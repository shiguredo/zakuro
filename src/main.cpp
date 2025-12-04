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

#include <blend2d/blend2d.h>

#include "duckdb_stats_writer.h"
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
  int http_port = 3960;
  std::string http_host = "127.0.0.1";
  bool ui = false;
  std::string ui_remote_url;
  std::string duckdb_dir;
  std::string connection_id_stats_file;
  double instance_hatch_rate = 1.0;
  ZakuroConfig config;
  Util::ParseArgs(args, config_file, log_level, http_port, http_host, ui,
                  ui_remote_url, duckdb_dir, connection_id_stats_file,
                  instance_hatch_rate, config, false);

  if (config_file.empty()) {
    // 設定ファイルが無ければそのまま ZakuroConfig を利用する
    configs.push_back(config);
  } else {
    // 設定ファイルがある場合は設定ファイルから引数を構築し直して再度パースする
    boost::json::value zakuro_value = Util::LoadJsoncFile(config_file);
    const auto& zakuro_obj = zakuro_value.as_object();
    std::vector<std::string> common_args;
    common_args.clear();

    if (zakuro_obj.contains("log-level")) {
      common_args.push_back("--log-level");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("log-level")));
    }
    if (zakuro_obj.contains("http-port")) {
      common_args.push_back("--http-port");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("http-port")));
    }
    if (zakuro_obj.contains("http-host")) {
      common_args.push_back("--http-host");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("http-host")));
    }
    if (zakuro_obj.contains("ui")) {
      if (zakuro_obj.at("ui").as_bool()) {
        common_args.push_back("--ui");
      }
    }
    if (zakuro_obj.contains("ui-remote-url")) {
      common_args.push_back("--ui-remote-url");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("ui-remote-url")));
    }
    if (zakuro_obj.contains("duckdb-dir")) {
      common_args.push_back("--duckdb-dir");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("duckdb-dir")));
    }
    if (zakuro_obj.contains("output-file-connection-id")) {
      common_args.push_back("--output-file-connection-id");
      common_args.push_back(Util::PrimitiveValueToString(
          zakuro_obj.at("output-file-connection-id")));
    }
    if (zakuro_obj.contains("instance-hatch-rate")) {
      common_args.push_back("--instance-hatch-rate");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("instance-hatch-rate")));
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

    if (!zakuro_obj.contains("instances")) {
      std::cerr << "instances キーがありません。" << std::endl;
      return 1;
    }
    const auto& instances_array = zakuro_obj.at("instances").as_array();
    if (instances_array.size() == 0) {
      std::cerr << "instances の下に設定がありません。" << std::endl;
      return 1;
    }
    for (const auto& instance : instances_array) {
      auto argss = Util::ParseInstanceToArgs(instance);
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
        Util::ParseArgs(args, config_file, log_level, http_port, http_host, ui,
                        ui_remote_url, duckdb_dir, connection_id_stats_file,
                        instance_hatch_rate, config, true);
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

  // DuckDB 統計ライターを初期化
  std::shared_ptr<DuckDBStatsWriter> duckdb_writer;
  if (!duckdb_dir.empty()) {
    duckdb_writer = std::make_shared<DuckDBStatsWriter>();
    if (!duckdb_writer->Initialize(duckdb_dir)) {
      std::cerr << "Failed to initialize DuckDB writer" << std::endl;
      return 1;
    }
    RTC_LOG(LS_INFO) << "DuckDB output: " << duckdb_writer->GetDbFilename();
    // 各 config に DuckDB ライターを設定
    for (auto& config : configs) {
      config.duckdb_writer = duckdb_writer;
    }
  }

  // --ui-remote-url は --ui と併用必須
  if (!ui_remote_url.empty() && !ui) {
    std::cerr << "--ui-remote-url を指定する場合は --ui も指定してください"
              << std::endl;
    return 1;
  }

  // HTTP サーバーの起動
  std::unique_ptr<HttpServer> http_server;
  http_server.reset(new HttpServer(http_port, http_host));
  // DuckDB ライターを設定
  if (duckdb_writer) {
    http_server->SetDuckDBWriter(duckdb_writer);
  }
  // --ui 指定時のみリバプロを有効化
  if (ui) {
    std::string remote_url = ui_remote_url.empty()
                                 ? "https://zakuro-ui.shiguredo.app/"
                                 : ui_remote_url;
    http_server->SetUIRemoteURL(remote_url);
    RTC_LOG(LS_INFO) << "UI remote URL set to: " << remote_url;
  }
  http_server->Start();
  RTC_LOG(LS_INFO) << "HTTP server started on " << http_host << ":"
                   << http_port;

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
