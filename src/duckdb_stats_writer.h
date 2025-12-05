#ifndef DUCKDB_STATS_WRITER_H_
#define DUCKDB_STATS_WRITER_H_

#include <duckdb.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/json/value.hpp>

#include "duckdb_utils.h"

// 前方宣言
struct VirtualClientStats;

class DuckDBStatsWriter {
 public:
  DuckDBStatsWriter();
  ~DuckDBStatsWriter();

  // データベースを初期化（日付ベースのファイル名を生成）
  bool Initialize(const std::string& base_path = ".");

  // 統計情報を書き込む
  bool WriteStats(const std::vector<VirtualClientStats>& stats);

  // WebRTC 統計情報を書き込む
  bool WriteRTCStats(const std::string& channel_id,
                     const std::string& session_id,
                     const std::string& connection_id,
                     const std::string& rtc_type,
                     double rtc_timestamp,
                     const std::string& rtc_data_json,
                     double timestamp);

  // zakuro 起動情報を書き込む
  bool WriteZakuroInfo(const std::string& config_file_path,
                       const std::string& global_config_json,
                       const std::string& instances_json);

  // クリーンアップ処理
  void Close();

  // SQL クエリを実行して結果を JSON で返す
  std::pair<bool, boost::json::value> ExecuteQuery(const std::string& query);

 private:
  duckdb_database db_{nullptr};
  duckdb_connection conn_{nullptr};
  std::mutex mutex_;
  bool initialized_{false};
  std::string db_filename_;

  // テーブルを作成
  void CreateTable();

  // ファイル名を生成（例: zakuro_20241226_123456_000.db）
  std::string GenerateFileName(const std::string& base_path);

  // リソースのクリーンアップ（エラー時の共通処理）
  void CleanupResources();
};

#endif  // DUCKDB_STATS_WRITER_H_
