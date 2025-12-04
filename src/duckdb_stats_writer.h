#ifndef DUCKDB_STATS_WRITER_H_
#define DUCKDB_STATS_WRITER_H_

#include <duckdb.h>

#include <mutex>
#include <string>
#include <vector>

#include "duckdb_utils.h"

struct VirtualClientStats;

class DuckDBStatsWriter {
 public:
  DuckDBStatsWriter();
  ~DuckDBStatsWriter();

  // コピー・ムーブ禁止
  DuckDBStatsWriter(const DuckDBStatsWriter&) = delete;
  DuckDBStatsWriter& operator=(const DuckDBStatsWriter&) = delete;
  DuckDBStatsWriter(DuckDBStatsWriter&&) = delete;
  DuckDBStatsWriter& operator=(DuckDBStatsWriter&&) = delete;

  // データベースを初期化（日付ベースのファイル名を生成）
  bool Initialize(const std::string& base_path);

  // 接続情報を書き込む
  bool WriteConnectionStats(const std::vector<VirtualClientStats>& stats);

  // WebRTC 統計情報を書き込む
  bool WriteRTCStats(const std::string& channel_id,
                     const std::string& session_id,
                     const std::string& connection_id,
                     const std::string& rtc_type,
                     double rtc_timestamp,
                     const std::string& rtc_data_json,
                     double timestamp);

  // zakuro 起動情報を書き込む
  bool WriteZakuroInfo(const std::string& config_mode,
                       const std::string& config_json);

  // クリーンアップ処理
  void Close();

  // SQL クエリを実行して JSON 形式で結果を返す
  std::string ExecuteQuery(const std::string& sql);

  // データベースファイル名を取得
  const std::string& GetDbFilename() const { return db_filename_; }

 private:
  duckdb_database db_{nullptr};
  duckdb_connection conn_{nullptr};
  std::mutex mutex_;
  bool initialized_{false};
  std::string db_filename_;

  void CreateTables();
  std::string GenerateFileName(const std::string& base_path);
  void CleanupResources();
};

#endif  // DUCKDB_STATS_WRITER_H_
