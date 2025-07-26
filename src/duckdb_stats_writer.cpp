#include "duckdb_stats_writer.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <rtc_base/logging.h>

DuckDBStatsWriter::DuckDBStatsWriter() = default;

DuckDBStatsWriter::~DuckDBStatsWriter() {
  Close();
}

bool DuckDBStatsWriter::Initialize(const std::string& base_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (initialized_) {
    return true;
  }
  
  try {
    std::string filename = GenerateFileName(base_path);
    RTC_LOG(LS_INFO) << "Creating DuckDB file: " << filename;
    
    // DuckDB インスタンスを作成
    db_ = std::make_unique<duckdb::DuckDB>(filename);
    conn_ = std::make_unique<duckdb::Connection>(*db_);
    
    // WAL モードを有効化
    conn_->Query("PRAGMA journal_mode=WAL");
    
    // テーブルを作成
    CreateTable();
    
    initialized_ = true;
    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to initialize DuckDB: " << e.what();
    return false;
  }
}

void DuckDBStatsWriter::CreateTable() {
  // 接続情報テーブルを作成
  std::string create_table_sql = R"(
    CREATE TABLE IF NOT EXISTS connections (
      pk BIGINT PRIMARY KEY DEFAULT nextval('connections_pk_seq'),
      timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      channel_id VARCHAR,
      connection_id VARCHAR,
      session_id VARCHAR,
      audio BOOLEAN,
      video BOOLEAN,
      websocket_connected BOOLEAN,
      datachannel_connected BOOLEAN
    )
  )";
  
  // シーケンスを作成
  auto seq_result = conn_->Query("CREATE SEQUENCE IF NOT EXISTS connections_pk_seq START 1");
  if (seq_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create sequence: " << seq_result->GetError();
    throw std::runtime_error("Failed to create sequence");
  }
  
  auto result = conn_->Query(create_table_sql);
  if (result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create table: " << result->GetError();
    throw std::runtime_error("Failed to create table");
  }
  
  // インデックスを作成
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_channel_id ON connections(channel_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_connection_id ON connections(connection_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_timestamp ON connections(timestamp)");
}

void DuckDBStatsWriter::WriteStats(const std::vector<VirtualClientStats>& stats) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING) << "DuckDBStatsWriter not initialized";
    return;
  }
  
  RTC_LOG(LS_INFO) << "Writing " << stats.size() << " stats to DuckDB";
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  try {
    // トランザクション開始
    conn_->Query("BEGIN TRANSACTION");
    
    // プリペアドステートメントを準備
    auto prepared = conn_->Prepare(
        "INSERT INTO connections (channel_id, connection_id, session_id, audio, video, "
        "websocket_connected, datachannel_connected) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)");
    
    if (prepared->HasError()) {
      RTC_LOG(LS_ERROR) << "Failed to prepare statement: " << prepared->GetError();
      conn_->Query("ROLLBACK");
      return;
    }
    
    // 各統計情報を挿入
    for (const auto& stat : stats) {
      // connection_id が空の場合はスキップ（まだ接続されていない）
      if (stat.connection_id.empty()) {
        RTC_LOG(LS_VERBOSE) << "Skipping stats with empty connection_id";
        continue;
      }
      
      auto result = prepared->Execute(
          stat.channel_id,
          stat.connection_id,
          stat.session_id,
          stat.has_audio_track,
          stat.has_video_track,
          stat.websocket_connected,
          stat.datachannel_connected
      );
      
      if (result->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to insert stats: " << result->GetError();
      }
    }
    
    // コミット
    conn_->Query("COMMIT");
    
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error writing stats: " << e.what();
    conn_->Query("ROLLBACK");
  }
}

void DuckDBStatsWriter::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (conn_) {
    // 最後のチェックポイントを実行
    conn_->Query("PRAGMA wal_checkpoint(TRUNCATE)");
    conn_.reset();
  }
  
  db_.reset();
  initialized_ = false;
}

std::string DuckDBStatsWriter::GenerateFileName(const std::string& base_path) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &time_t);
#else
  localtime_r(&time_t, &tm);
#endif
  
  std::ostringstream oss;
  oss << base_path << "/zakuro_stats_"
      << std::put_time(&tm, "%Y%m%d_%H%M%S")
      << "_" << std::setfill('0') << std::setw(3) << ms.count()
      << ".ddb";
  
  return oss.str();
}