#ifndef DUCKDB_STATS_WRITER_H_
#define DUCKDB_STATS_WRITER_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <duckdb.h>

#include "duckdb_utils.h"
#include "virtual_client.h"

class DuckDBStatsWriter {
 public:
  DuckDBStatsWriter();
  ~DuckDBStatsWriter();

  // データベースを初期化（日付ベースのファイル名を生成）
  bool Initialize(const std::string& base_path = ".");

  // 統計情報を書き込む
  bool WriteStats(const std::vector<VirtualClientStats>& stats);

  // WebRTC統計情報を書き込む
  bool WriteRTCStats(const std::string& channel_id,
                     const std::string& session_id,
                     const std::string& connection_id,
                     const std::string& rtc_type,
                     double rtc_timestamp,
                     const std::string& rtc_data_json,
                     double timestamp);

  // クリーンアップ処理
  void Close();

  // SQLクエリを実行してJSON形式で結果を返す
  std::string ExecuteQuery(const std::string& sql);

 private:
  duckdb_database db_{nullptr};
  duckdb_connection conn_{nullptr};
  std::mutex mutex_;
  bool initialized_{false};
  std::string db_filename_;  // 現在のデータベースファイル名


  // テーブルを作成
  void CreateTable();


  // ファイル名を生成（例: zakuro_stats_20241226_123456.db）
  std::string GenerateFileName(const std::string& base_path);

  // リソースのクリーンアップ（エラー時の共通処理）
  void CleanupResources();
};

#endif  // DUCKDB_STATS_WRITER_H_