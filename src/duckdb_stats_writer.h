#ifndef DUCKDB_STATS_WRITER_H_
#define DUCKDB_STATS_WRITER_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <duckdb.hpp>

#include "virtual_client.h"

class DuckDBStatsWriter {
 public:
  DuckDBStatsWriter();
  ~DuckDBStatsWriter();

  // データベースを初期化（日付ベースのファイル名を生成）
  bool Initialize(const std::string& base_path = ".");
  
  // 統計情報を書き込む
  void WriteStats(const std::vector<VirtualClientStats>& stats);
  
  // WebRTC統計情報を書き込む
  void WriteRTCStats(const std::string& channel_id,
                     const std::string& session_id,
                     const std::string& connection_id,
                     const std::string& rtc_type,
                     double rtc_timestamp,
                     const std::string& rtc_data_json);
  
  // クリーンアップ処理
  void Close();

 private:
  std::unique_ptr<duckdb::DuckDB> db_;
  std::unique_ptr<duckdb::Connection> conn_;
  std::mutex mutex_;
  bool initialized_{false};
  
  // テーブルを作成
  void CreateTable();
  
  // ファイル名を生成（例: zakuro_stats_20241226_123456.db）
  std::string GenerateFileName(const std::string& base_path);
};

#endif  // DUCKDB_STATS_WRITER_H_