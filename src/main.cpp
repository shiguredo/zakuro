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

// シグナルハンドラー用のグローバル変数
static std::atomic<bool> g_shutdown_requested{false};
static std::shared_ptr<DuckDBStatsWriter> g_duckdb_writer;

void SignalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    RTC_LOG(LS_INFO) << "Shutdown signal received: " << signal;
    g_shutdown_requested = true;

    // DuckDB をクローズ
    if (g_duckdb_writer) {
      g_duckdb_writer->Close();
    }
  }
}

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
  // シグナルハンドラーを設定
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
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
  std::string http_port = "none";
  std::string http_host = "127.0.0.1";
  std::string ui_remote_url = "http://localhost:5173";
  std::string connection_id_stats_file;
  double instance_hatch_rate = 1.0;
  ZakuroConfig config;
  Util::ParseArgs(args, config_file, log_level, http_port, http_host, ui_remote_url,
                  connection_id_stats_file, instance_hatch_rate, config, false);

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
    if (zakuro_obj.contains("port")) {
      common_args.push_back("--port");
      common_args.push_back(
          Util::PrimitiveValueToString(zakuro_obj.at("port")));
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
    if (zakuro_node["rtc-stats-interval"]) {
      common_args.push_back("--rtc-stats-interval");
      common_args.push_back(
          zakuro_node["rtc-stats-interval"].as<std::string>());
    }
    if (zakuro_node["duckdb-output-dir"]) {
      common_args.push_back("--duckdb-output-dir");
      common_args.push_back(
          zakuro_node["duckdb-output-dir"].as<std::string>());
    }
    if (zakuro_node["no-duckdb-output"] && 
        zakuro_node["no-duckdb-output"].as<bool>()) {
      common_args.push_back("--no-duckdb-output");
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
        Util::ParseArgs(args, config_file, log_level, http_port, http_host, ui_remote_url,
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
      new webrtc::FileRotatingLogSink("./", "webrtc_logs",
                                      kDefaultMaxLogFileSize, 10));
  if (!log_sink->Init()) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "Failed to open log file";
    log_sink.reset();
    return 1;
  }
  webrtc::LogMessage::AddLogToStream(log_sink.get(), webrtc::LS_INFO);

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

  // DuckDB 統計ライターを初期化
  std::shared_ptr<DuckDBStatsWriter> duckdb_writer;
  
  // 最初の config から DuckDB 設定を取得
  bool no_duckdb_output = !configs.empty() ? configs[0].no_duckdb_output : false;
  std::string duckdb_output_dir = !configs.empty() && !configs[0].duckdb_output_dir.empty() 
                                   ? configs[0].duckdb_output_dir : ".";
  
  if (!no_duckdb_output) {
    duckdb_writer.reset(new DuckDBStatsWriter());
    if (!duckdb_writer->Initialize(duckdb_output_dir)) {
      RTC_LOG(LS_ERROR) << "Failed to initialize DuckDB stats writer in directory: " 
                        << duckdb_output_dir;
      // エラーでも続行（統計情報の記録は必須ではない）
    } else {
      
      // zakuro 起動情報を保存
      std::string config_mode = config_file.empty() ? "ARGS" : "YAML";
      boost::json::object config_json;
      
      if (config_file.empty()) {
        // 引数モードの場合、主要な設定をJSONに保存
        if (!configs.empty()) {
          const auto& cfg = configs[0];
          config_json["scenario"] = cfg.scenario;
          config_json["vcs"] = cfg.vcs;
          config_json["duration"] = cfg.duration;
          config_json["repeat_interval"] = cfg.repeat_interval;
          config_json["max_retry"] = cfg.max_retry;
          config_json["retry_interval"] = cfg.retry_interval;
          config_json["sora_channel_id"] = cfg.sora_channel_id;
          config_json["sora_role"] = cfg.sora_role;
          
          // sora_signaling_urls を配列として保存
          boost::json::array urls;
          for (const auto& url : cfg.sora_signaling_urls) {
            urls.push_back(boost::json::value(url));
          }
          config_json["sora_signaling_urls"] = urls;
        }
      } else {
        // YAML モードの場合、YAML ファイルの内容を JSON として保存
        try {
          YAML::Node yaml_node = YAML::LoadFile(config_file);
          boost::json::value yaml_json = Util::NodeToJson(yaml_node);
          // boost::json::value を boost::json::object に変換
          if (yaml_json.is_object()) {
            config_json = yaml_json.as_object();
          } else {
            // YAML のルートがオブジェクトでない場合は、オブジェクトでラップ
            config_json["data"] = yaml_json;
          }
        } catch (const std::exception& e) {
          RTC_LOG(LS_WARNING) << "Failed to convert YAML to JSON: " << e.what();
          config_json["error"] = "Failed to convert YAML to JSON";
        }
      }
      
      // zakuro 情報を DuckDB に書き込む
      std::string config_json_str = boost::json::serialize(config_json);
      if (!duckdb_writer->WriteZakuroInfo(config_mode, config_json_str)) {
        RTC_LOG(LS_WARNING) << "Failed to write zakuro info to DuckDB";
      }
      
      // シナリオ情報を書き込む
      if (!configs.empty()) {
        const auto& cfg = configs[0];
        if (!duckdb_writer->WriteZakuroScenario(
                cfg.vcs,
                cfg.duration,
                cfg.repeat_interval,
                cfg.max_retry,
                cfg.retry_interval,
                cfg.sora_signaling_urls,
                cfg.sora_channel_id,
                cfg.sora_role)) {
          RTC_LOG(LS_WARNING) << "Failed to write zakuro scenario to DuckDB";
        }
      }
    }
    g_duckdb_writer = duckdb_writer;  // グローバル変数に設定（シグナルハンドラー用）
  }

  // 各 config に duckdb_writer を設定
  for (auto& config : configs) {
    config.duckdb_writer = duckdb_writer;
  }

  // ユニークな番号を設定
  for (int i = 0; i < configs.size(); i++) {
    configs[i].id = i;
  }

  // HTTP サーバーの起動
  std::unique_ptr<HttpServer> http_server;
  if (http_port != "none") {
    try {
      int port = std::stoi(http_port);
      if (port < 1 || port > 65535) {
        RTC_LOG(LS_ERROR) << "Invalid HTTP port number: " << port;
        return 1;
      }
      http_server.reset(new HttpServer(port, http_host));
      http_server->SetDuckDBWriter(duckdb_writer);
      http_server->SetUIRemoteURL(ui_remote_url);
      http_server->Start();
      std::cout << "HTTP server started on " << http_host << ":" << port
                << " - http://" << http_host << ":" << port << "/" << std::endl;
    } catch (const std::exception& e) {
      RTC_LOG(LS_ERROR) << "Invalid HTTP port value: " << http_port;
      return 1;
    }
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
        // connection_id_stats_file が指定されている場合はJSONファイルに出力
        if (!connection_id_stats_file.empty()) {
          std::string jstr = boost::json::serialize(obj);
          // ファイルに出力
          std::ofstream ofs(connection_id_stats_file);
          ofs << jstr;
        }
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

  // DuckDB をクローズ
  if (duckdb_writer) {
    duckdb_writer->Close();
  }

  return 0;
}
