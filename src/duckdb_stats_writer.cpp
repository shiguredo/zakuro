#include "duckdb_stats_writer.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <rtc_base/logging.h>
#include <boost/json.hpp>

namespace {
// ファイル名生成時の定数
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMillisecondFieldWidth = 3;
}  // namespace

DuckDBStatsWriter::DuckDBStatsWriter() = default;

DuckDBStatsWriter::~DuckDBStatsWriter() {
  Close();
}

bool DuckDBStatsWriter::Initialize(const std::string& base_path) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    return true;
  }

  // DuckDB library version を確認
  const char* lib_version = duckdb_library_version();

  std::string filename = GenerateFileName(base_path);
  db_filename_ = filename;  // ファイル名を保存
  RTC_LOG(LS_VERBOSE) << "DuckDB file: " << filename;

  // DuckDB インスタンスを作成
  char* error_message = nullptr;
  auto open_result =
      duckdb_open_ext(filename.c_str(), &db_, nullptr, &error_message);
  if (open_result == DuckDBError) {
    RTC_LOG(LS_ERROR) << "Failed to open DuckDB file: " << filename;
    if (error_message) {
      RTC_LOG(LS_ERROR) << "DuckDB error: " << error_message;
      duckdb_free(error_message);
    }
    return false;
  }

  if (duckdb_connect(db_, &conn_) == DuckDBError) {
    RTC_LOG(LS_ERROR) << "Failed to create connection";
    duckdb_close(&db_);
    db_ = nullptr;
    return false;
  }

  // WALモードはデフォルトで有効

  try {
    // テーブルを作成
    CreateTable();

    // PreparedStatementを準備
    if (!PrepareCachedStatements()) {
      RTC_LOG(LS_ERROR) << "Failed to prepare cached statements";
      // リソースを確実に解放（順序が重要）
      if (conn_) {
        duckdb_disconnect(&conn_);
        conn_ = nullptr;
      }
      if (db_) {
        duckdb_close(&db_);
        db_ = nullptr;
      }
      return false;
    }
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to create tables: " << e.what();
    // リソースを確実に解放（順序が重要）
    if (conn_) {
      duckdb_disconnect(&conn_);
      conn_ = nullptr;
    }
    if (db_) {
      duckdb_close(&db_);
      db_ = nullptr;
    }
    return false;
  }

  initialized_ = true;
  return true;
}

void DuckDBStatsWriter::CreateTable() {
  // ヘルパー関数：クエリを実行して結果を破棄
  auto execute_query = [this](const std::string& query) {
    duckdb_result result;
    if (duckdb_query(conn_, query.c_str(), &result) == DuckDBError) {
      std::string error_msg = duckdb_result_error(&result);
      duckdb_destroy_result(&result);
      RTC_LOG(LS_ERROR) << "Query failed: " << error_msg;
      throw std::runtime_error("Query failed: " + error_msg);
    }
    duckdb_destroy_result(&result);
  };

  // シーケンスを作成
  execute_query("CREATE SEQUENCE IF NOT EXISTS connections_pk_seq START 1");
  execute_query("CREATE SEQUENCE IF NOT EXISTS codec_stats_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS inbound_rtp_stats_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS outbound_rtp_stats_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS media_source_stats_pk_seq START 1");

  // 接続情報テーブルを作成
  std::string create_table_sql = R"(
    CREATE TABLE IF NOT EXISTS connections (
      pk BIGINT PRIMARY KEY DEFAULT nextval('connections_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      connection_id VARCHAR,
      session_id VARCHAR,
      audio BOOLEAN,
      video BOOLEAN,
      websocket_connected BOOLEAN,
      datachannel_connected BOOLEAN
    )
  )";
  execute_query(create_table_sql);

  // codec統計情報テーブルを作成
  std::string create_codec_table_sql = R"(
    CREATE TABLE IF NOT EXISTS codec_stats (
      pk BIGINT PRIMARY KEY DEFAULT nextval('codec_stats_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      mime_type VARCHAR,
      payload_type BIGINT,
      clock_rate BIGINT,
      channels BIGINT,
      sdp_fmtp_line VARCHAR,
      UNIQUE(connection_id, id, mime_type, payload_type, clock_rate, channels, sdp_fmtp_line)
    )
  )";
  execute_query(create_codec_table_sql);

  // inbound-rtp統計情報テーブルを作成
  std::string create_inbound_table_sql = R"(
    CREATE TABLE IF NOT EXISTS inbound_rtp_stats (
      pk BIGINT PRIMARY KEY DEFAULT nextval('inbound_rtp_stats_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      -- RTCStats
      type VARCHAR,
      id VARCHAR,
      -- RTCRtpStreamStats
      ssrc BIGINT,
      kind VARCHAR,
      transport_id VARCHAR,
      codec_id VARCHAR,
      -- RTCReceivedRtpStreamStats
      packets_received BIGINT,
      packets_lost BIGINT,
      bytes_received BIGINT,
      jitter DOUBLE,
      packets_received_with_ect1 BIGINT,
      packets_received_with_ce BIGINT,
      packets_reported_as_lost BIGINT,
      packets_reported_as_lost_but_recovered BIGINT,
      -- RTCInboundRtpStreamStats
      last_packet_received_timestamp DOUBLE,
      header_bytes_received BIGINT,
      packets_discarded BIGINT,
      fec_bytes_received BIGINT,
      fec_packets_received BIGINT,
      fec_packets_discarded BIGINT,
      nack_count BIGINT,
      pli_count BIGINT,
      fir_count BIGINT,
      track_identifier VARCHAR,
      mid VARCHAR,
      remote_id VARCHAR,
      frames_decoded BIGINT,
      key_frames_decoded BIGINT,
      frames_rendered BIGINT,
      frames_dropped BIGINT,
      frame_width BIGINT,
      frame_height BIGINT,
      frames_per_second DOUBLE,
      qp_sum BIGINT,
      total_decode_time DOUBLE,
      total_inter_frame_delay DOUBLE,
      total_squared_inter_frame_delay DOUBLE,
      pause_count BIGINT,
      total_pauses_duration DOUBLE,
      freeze_count BIGINT,
      total_freezes_duration DOUBLE,
      total_processing_delay DOUBLE,
      estimated_playout_timestamp DOUBLE,
      jitter_buffer_delay DOUBLE,
      jitter_buffer_target_delay DOUBLE,
      jitter_buffer_emitted_count BIGINT,
      jitter_buffer_minimum_delay DOUBLE,
      total_samples_received BIGINT,
      concealed_samples BIGINT,
      silent_concealed_samples BIGINT,
      concealment_events BIGINT,
      inserted_samples_for_deceleration BIGINT,
      removed_samples_for_acceleration BIGINT,
      audio_level DOUBLE,
      total_audio_energy DOUBLE,
      total_samples_duration DOUBLE,
      frames_received BIGINT,
      decoder_implementation VARCHAR,
      playout_id VARCHAR,
      power_efficient_decoder BOOLEAN,
      frames_assembled_from_multiple_packets BIGINT,
      total_assembly_time DOUBLE,
      retransmitted_packets_received BIGINT,
      retransmitted_bytes_received BIGINT,
      rtx_ssrc BIGINT,
      fec_ssrc BIGINT,
      total_corruption_probability DOUBLE,
      total_squared_corruption_probability DOUBLE,
      corruption_measurements BIGINT
    )
  )";
  execute_query(create_inbound_table_sql);

  // outbound-rtp統計情報テーブルを作成
  std::string create_outbound_table_sql = R"(
    CREATE TABLE IF NOT EXISTS outbound_rtp_stats (
      pk BIGINT PRIMARY KEY DEFAULT nextval('outbound_rtp_stats_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      -- RTCStats
      type VARCHAR,
      id VARCHAR,
      -- RTCRtpStreamStats
      ssrc BIGINT,
      kind VARCHAR,
      transport_id VARCHAR,
      codec_id VARCHAR,
      -- RTCSentRtpStreamStats
      packets_sent BIGINT,
      bytes_sent BIGINT,
      packets_sent_with_ect1 BIGINT,
      -- RTCOutboundRtpStreamStats
      mid VARCHAR,
      media_source_id VARCHAR,
      remote_id VARCHAR,
      rid VARCHAR,
      encoding_index BIGINT,
      header_bytes_sent BIGINT,
      retransmitted_packets_sent BIGINT,
      retransmitted_bytes_sent BIGINT,
      rtx_ssrc BIGINT,
      target_bitrate DOUBLE,
      total_encoded_bytes_target BIGINT,
      frame_width BIGINT,
      frame_height BIGINT,
      frames_per_second DOUBLE,
      frames_sent BIGINT,
      huge_frames_sent BIGINT,
      frames_encoded BIGINT,
      key_frames_encoded BIGINT,
      qp_sum BIGINT,
      -- psnrSum と psnrMeasurements は record<DOMString, double> 型なので別途処理が必要
      total_encode_time DOUBLE,
      total_packet_send_delay DOUBLE,
      quality_limitation_reason VARCHAR,
      quality_limitation_duration_none DOUBLE,
      quality_limitation_duration_cpu DOUBLE,
      quality_limitation_duration_bandwidth DOUBLE,
      quality_limitation_duration_other DOUBLE,
      quality_limitation_resolution_changes BIGINT,
      nack_count BIGINT,
      pli_count BIGINT,
      fir_count BIGINT,
      encoder_implementation VARCHAR,
      power_efficient_encoder BOOLEAN,
      active BOOLEAN,
      scalability_mode VARCHAR
    )
  )";
  execute_query(create_outbound_table_sql);

  // media-source統計情報テーブルを作成
  std::string create_media_source_table_sql = R"(
    CREATE TABLE IF NOT EXISTS media_source_stats (
      pk BIGINT PRIMARY KEY DEFAULT nextval('media_source_stats_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      -- RTCStats
      type VARCHAR,
      id VARCHAR,
      -- RTCMediaSourceStats
      track_identifier VARCHAR,
      kind VARCHAR,
      -- RTCAudioSourceStats
      audio_level DOUBLE,
      total_audio_energy DOUBLE,
      total_samples_duration DOUBLE,
      echo_return_loss DOUBLE,
      echo_return_loss_enhancement DOUBLE,
      -- RTCVideoSourceStats
      width BIGINT,
      height BIGINT,
      frames BIGINT,
      frames_per_second DOUBLE
    )
  )";
  execute_query(create_media_source_table_sql);

  // インデックスを作成
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_channel_id ON connections(channel_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_connection_id ON "
      "connections(connection_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_timestamp ON connections(timestamp)");

  // 各統計情報テーブルのインデックスを作成
  // 複合インデックスを追加
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_codec_composite ON "
      "codec_stats(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_codec_channel_id ON "
      "codec_stats(channel_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_codec_connection_id ON "
      "codec_stats(connection_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_codec_timestamp ON "
      "codec_stats(timestamp)");

  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_inbound_composite ON "
      "inbound_rtp_stats(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_inbound_channel_id ON "
      "inbound_rtp_stats(channel_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_inbound_connection_id ON "
      "inbound_rtp_stats(connection_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_inbound_timestamp ON "
      "inbound_rtp_stats(timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_inbound_kind ON inbound_rtp_stats(kind)");

  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_outbound_composite ON "
      "outbound_rtp_stats(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_outbound_channel_id ON "
      "outbound_rtp_stats(channel_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_outbound_connection_id ON "
      "outbound_rtp_stats(connection_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_outbound_timestamp ON "
      "outbound_rtp_stats(timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_outbound_kind ON "
      "outbound_rtp_stats(kind)");

  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_media_source_composite ON "
      "media_source_stats(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_media_source_channel_id ON "
      "media_source_stats(channel_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_media_source_connection_id ON "
      "media_source_stats(connection_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_media_source_timestamp ON "
      "media_source_stats(timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_media_source_kind ON "
      "media_source_stats(kind)");
}

bool DuckDBStatsWriter::PrepareCachedStatements() {
  // connections用のPreparedStatement
  connections_stmt_ = std::make_unique<duckdb_utils::PreparedStatement>();
  const char* connections_sql =
      "INSERT INTO connections (timestamp, channel_id, connection_id, "
      "session_id, audio, video, "
      "websocket_connected, datachannel_connected) "
      "VALUES (CURRENT_TIMESTAMP, $1, $2, $3, $4, $5, $6, $7)";
  if (!duckdb_utils::Prepare(conn_, connections_sql, *connections_stmt_)) {
    RTC_LOG(LS_ERROR) << "Failed to prepare connections statement: "
                      << connections_stmt_->error();
    return false;
  }

  // codec_stats用のPreparedStatement
  codec_stats_stmt_ = std::make_unique<duckdb_utils::PreparedStatement>();
  const char* codec_sql =
      "INSERT INTO codec_stats (timestamp, channel_id, session_id, "
      "connection_id, rtc_timestamp, "
      "type, id, mime_type, payload_type, clock_rate, channels, "
      "sdp_fmtp_line) "
      "VALUES (TO_TIMESTAMP($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, "
      "$12) "
      "ON CONFLICT (connection_id, id, mime_type, payload_type, "
      "clock_rate, channels, sdp_fmtp_line) "
      "DO NOTHING";
  if (!duckdb_utils::Prepare(conn_, codec_sql, *codec_stats_stmt_)) {
    RTC_LOG(LS_ERROR) << "Failed to prepare codec stats statement: "
                      << codec_stats_stmt_->error();
    return false;
  }

  // 他のPreparedStatementも同様に準備できますが、
  // 使用頻度が高いものから順に実装します

  return true;
}

bool DuckDBStatsWriter::WriteStats(
    const std::vector<VirtualClientStats>& stats) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING) << "DuckDBStatsWriter::WriteStats - not initialized";
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  try {
    duckdb_utils::Transaction transaction(conn_);

    // 各統計情報を挿入
    int inserted_count = 0;
    for (const auto& stat : stats) {
      // connection_id が空の場合はスキップ（まだ接続されていない）
      if (stat.connection_id.empty()) {
        continue;
      }

      // キャッシュされたPreparedStatementを使用
      // 毎回clearしてから使用
      duckdb_clear_bindings(connections_stmt_->get_raw());

      // パラメータをバインド
      duckdb_bind_varchar(connections_stmt_->get_raw(), 1,
                          stat.channel_id.c_str());
      duckdb_bind_varchar(connections_stmt_->get_raw(), 2,
                          stat.connection_id.c_str());
      duckdb_bind_varchar(connections_stmt_->get_raw(), 3,
                          stat.session_id.c_str());
      duckdb_bind_boolean(connections_stmt_->get_raw(), 4,
                          stat.has_audio_track);
      duckdb_bind_boolean(connections_stmt_->get_raw(), 5,
                          stat.has_video_track);
      duckdb_bind_boolean(connections_stmt_->get_raw(), 6,
                          stat.websocket_connected);
      duckdb_bind_boolean(connections_stmt_->get_raw(), 7,
                          stat.datachannel_connected);

      // 実行
      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(connections_stmt_->get_raw(),
                                         exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert stats for connection_id="
                          << stat.connection_id << ": " << exec_result.error();
        // エラーが発生したら、トランザクション全体をロールバックするために例外をスロー
        throw std::runtime_error("Failed to insert stats: " +
                                 exec_result.error());
      }
      inserted_count++;
    }

    // コミット
    transaction.Commit();

    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error in WriteStats: " << e.what();
    // トランザクションは自動的にロールバックされる
    return false;
  }
}

bool DuckDBStatsWriter::WriteRTCStats(const std::string& channel_id,
                                      const std::string& session_id,
                                      const std::string& connection_id,
                                      const std::string& rtc_type,
                                      double rtc_timestamp,
                                      const std::string& rtc_data_json,
                                      double timestamp) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING) << "DuckDBStatsWriter::WriteRTCStats - not initialized"
                        << " rtc_type=" << rtc_type
                        << " connection_id=" << connection_id;
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  try {
    // JSON文字列をパース
    auto json = boost::json::parse(rtc_data_json);
    auto json_obj = json.as_object();

    // 値を取得するヘルパー関数
    auto get_string = [&json_obj](
                          const std::string& key,
                          const std::string& default_val = "") -> std::string {
      if (json_obj.contains(key) && json_obj.at(key).is_string()) {
        return std::string(json_obj.at(key).as_string());
      }
      return default_val;
    };

    auto get_int64 = [&json_obj](const std::string& key,
                                 int64_t default_val = 0) -> int64_t {
      if (json_obj.contains(key)) {
        if (json_obj.at(key).is_int64())
          return json_obj.at(key).as_int64();
        if (json_obj.at(key).is_uint64())
          return static_cast<int64_t>(json_obj.at(key).as_uint64());
      }
      return default_val;
    };

    auto get_double = [&json_obj](const std::string& key,
                                  double default_val = 0.0) -> double {
      if (json_obj.contains(key)) {
        if (json_obj.at(key).is_double())
          return json_obj.at(key).as_double();
        if (json_obj.at(key).is_int64())
          return static_cast<double>(json_obj.at(key).as_int64());
        if (json_obj.at(key).is_uint64())
          return static_cast<double>(json_obj.at(key).as_uint64());
      }
      return default_val;
    };

    auto get_bool = [&json_obj](const std::string& key,
                                bool default_val = false) -> bool {
      if (json_obj.contains(key) && json_obj.at(key).is_bool()) {
        return json_obj.at(key).as_bool();
      }
      return default_val;
    };

    // rtc_typeに応じて適切なテーブルに挿入
    if (rtc_type == "codec") {
      // codec統計情報の挿入処理
      // キャッシュされたPreparedStatementを使用
      duckdb_clear_bindings(codec_stats_stmt_->get_raw());

      // パラメータをバインド
      duckdb_bind_double(codec_stats_stmt_->get_raw(), 1, timestamp);
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 2, channel_id.c_str());
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 3, session_id.c_str());
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 4,
                          connection_id.c_str());
      duckdb_bind_double(codec_stats_stmt_->get_raw(), 5, rtc_timestamp);
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 6,
                          get_string("type").c_str());
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 7,
                          get_string("id").c_str());
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 8,
                          get_string("mimeType").c_str());
      duckdb_bind_int64(codec_stats_stmt_->get_raw(), 9,
                        get_int64("payloadType"));
      duckdb_bind_int64(codec_stats_stmt_->get_raw(), 10,
                        get_int64("clockRate"));
      duckdb_bind_int64(codec_stats_stmt_->get_raw(), 11,
                        get_int64("channels"));
      duckdb_bind_varchar(codec_stats_stmt_->get_raw(), 12,
                          get_string("sdpFmtpLine").c_str());

      // 実行
      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(codec_stats_stmt_->get_raw(),
                                         exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert codec stats: "
                          << exec_result.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error("Failed to insert codec stats: " +
                                 exec_result.error());
      }

    } else if (rtc_type == "inbound-rtp") {
      // inbound-rtp統計情報の挿入処理
      duckdb_utils::PreparedStatement stmt;
      const char* prepare_sql =
          "INSERT INTO inbound_rtp_stats (timestamp, channel_id, session_id, "
          "connection_id, rtc_timestamp, "
          "type, id, ssrc, kind, transport_id, codec_id, "
          "packets_received, packets_lost, bytes_received, jitter, "
          "last_packet_received_timestamp, header_bytes_received, "
          "packets_discarded, "
          "fec_bytes_received, fec_packets_received, fec_packets_discarded, "
          "nack_count, pli_count, fir_count, track_identifier, mid, remote_id, "
          "frames_decoded, key_frames_decoded, frames_rendered, "
          "frames_dropped, "
          "frame_width, frame_height, frames_per_second, qp_sum, "
          "total_decode_time, total_inter_frame_delay, "
          "total_squared_inter_frame_delay, "
          "pause_count, total_pauses_duration, freeze_count, "
          "total_freezes_duration, "
          "total_processing_delay, estimated_playout_timestamp, "
          "jitter_buffer_delay, "
          "jitter_buffer_target_delay, jitter_buffer_emitted_count, "
          "jitter_buffer_minimum_delay, "
          "total_samples_received, concealed_samples, "
          "silent_concealed_samples, concealment_events, "
          "inserted_samples_for_deceleration, "
          "removed_samples_for_acceleration, "
          "audio_level, total_audio_energy, total_samples_duration, "
          "frames_received, "
          "decoder_implementation, playout_id, power_efficient_decoder, "
          "frames_assembled_from_multiple_packets, total_assembly_time, "
          "retransmitted_packets_received, retransmitted_bytes_received, "
          "rtx_ssrc, fec_ssrc) "
          "VALUES (CURRENT_TIMESTAMP, $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
          "$11, $12, $13, $14, $15, $16, $17, $18, $19, $20, "
          "$21, $22, $23, $24, $25, $26, $27, $28, $29, $30, "
          "$31, $32, $33, $34, $35, $36, $37, $38, $39, $40, "
          "$41, $42, $43, $44, $45, $46, $47, $48, $49, $50, "
          "$51, $52, $53, $54, $55, $56, $57, $58, $59, $60, "
          "$61, $62, $63)";

      if (!duckdb_utils::Prepare(conn_, prepare_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare inbound-rtp stats statement: "
                          << stmt.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error(
            "Failed to prepare inbound-rtp stats statement: " + stmt.error());
      }

      // パラメータをバインド
      int idx = 1;
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("id").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("ssrc"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("kind").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("transportId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("codecId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("packetsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("packetsLost"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("bytesReceived"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("jitter"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("lastPacketReceivedTimestamp"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("headerBytesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("packetsDiscarded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("fecBytesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("fecPacketsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("fecPacketsDiscarded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("nackCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("pliCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("firCount"));
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("trackIdentifier").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("mid").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("remoteId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("framesDecoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("keyFramesDecoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("framesRendered"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("framesDropped"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("frameWidth"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("frameHeight"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("framesPerSecond"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("qpSum"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("totalDecodeTime"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalInterFrameDelay"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalSquaredInterFrameDelay"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("pauseCount"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalPausesDuration"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("freezeCount"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalFreezesDuration"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalProcessingDelay"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("estimatedPlayoutTimestamp"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("jitterBufferDelay"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("jitterBufferTargetDelay"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("jitterBufferEmittedCount"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("jitterBufferMinimumDelay"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("totalSamplesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("concealedSamples"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("silentConcealedSamples"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("concealmentEvents"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("insertedSamplesForDeceleration"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("removedSamplesForAcceleration"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("audioLevel"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("totalAudioEnergy"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalSamplesDuration"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("framesReceived"));
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("decoderImplementation").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("playoutId").c_str());
      duckdb_bind_boolean(stmt.get_raw(), idx++,
                          get_bool("powerEfficientDecoder"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("framesAssembledFromMultiplePackets"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalAssemblyTime"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("retransmittedPacketsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("retransmittedBytesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("rtxSsrc"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("fecSsrc"));

      // 実行
      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert inbound-rtp stats: "
                          << exec_result.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error("Failed to insert inbound-rtp stats: " +
                                 exec_result.error());
      }

    } else if (rtc_type == "outbound-rtp") {
      // outbound-rtp統計情報の挿入処理
      duckdb_utils::PreparedStatement stmt;
      const char* prepare_sql =
          "INSERT INTO outbound_rtp_stats (timestamp, channel_id, session_id, "
          "connection_id, rtc_timestamp, "
          "type, id, ssrc, kind, transport_id, codec_id, "
          "packets_sent, bytes_sent, packets_sent_with_ect1, "
          "mid, media_source_id, remote_id, rid, encoding_index, "
          "header_bytes_sent, retransmitted_packets_sent, "
          "retransmitted_bytes_sent, "
          "rtx_ssrc, target_bitrate, total_encoded_bytes_target, "
          "frame_width, frame_height, frames_per_second, frames_sent, "
          "huge_frames_sent, frames_encoded, key_frames_encoded, qp_sum, "
          "total_encode_time, total_packet_send_delay, "
          "quality_limitation_reason, quality_limitation_duration_none, "
          "quality_limitation_duration_cpu, "
          "quality_limitation_duration_bandwidth, "
          "quality_limitation_duration_other, "
          "quality_limitation_resolution_changes, "
          "nack_count, pli_count, fir_count, encoder_implementation, "
          "power_efficient_encoder, active, scalability_mode) "
          "VALUES (CURRENT_TIMESTAMP, $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
          "$11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, "
          "$22, $23, $24, $25, $26, $27, $28, $29, $30, $31, $32, "
          "$33, $34, $35, $36, $37, $38, $39, $40, $41, $42, $43, "
          "$44, $45, $46, $47)";

      if (!duckdb_utils::Prepare(conn_, prepare_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare outbound-rtp stats statement: "
                          << stmt.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error(
            "Failed to prepare outbound-rtp stats statement: " + stmt.error());
      }

      // パラメータをバインド
      int idx = 1;
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("id").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("ssrc"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("kind").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("transportId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("codecId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("packetsSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("bytesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("packetsSentWithEct1"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("mid").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("mediaSourceId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("remoteId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("rid").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("encodingIndex"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("headerBytesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("retransmittedPacketsSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("retransmittedBytesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("rtxSsrc"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("targetBitrate"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("totalEncodedBytesTarget"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("frameWidth"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("frameHeight"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("framesPerSecond"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("framesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("hugeFramesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("framesEncoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("keyFramesEncoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("qpSum"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("totalEncodeTime"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalPacketSendDelay"));
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("qualityLimitationReason").c_str());
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("qualityLimitationDurationNone"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("qualityLimitationDurationCpu"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("qualityLimitationDurationBandwidth"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("qualityLimitationDurationOther"));
      duckdb_bind_int64(stmt.get_raw(), idx++,
                        get_int64("qualityLimitationResolutionChanges"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("nackCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("pliCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("firCount"));
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("encoderImplementation").c_str());
      duckdb_bind_boolean(stmt.get_raw(), idx++,
                          get_bool("powerEfficientEncoder"));
      duckdb_bind_boolean(stmt.get_raw(), idx++, get_bool("active"));
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("scalabilityMode").c_str());

      // 実行
      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert outbound-rtp stats: "
                          << exec_result.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error("Failed to insert outbound-rtp stats: " +
                                 exec_result.error());
      }

    } else if (rtc_type == "media-source") {
      // media-source統計情報の挿入処理
      duckdb_utils::PreparedStatement stmt;
      const char* prepare_sql =
          "INSERT INTO media_source_stats (timestamp, channel_id, session_id, "
          "connection_id, rtc_timestamp, "
          "type, id, track_identifier, kind, "
          "audio_level, total_audio_energy, total_samples_duration, "
          "echo_return_loss, echo_return_loss_enhancement, "
          "width, height, frames, frames_per_second) "
          "VALUES (CURRENT_TIMESTAMP, $1, $2, $3, $4, $5, $6, $7, $8, "
          "$9, $10, $11, $12, $13, $14, $15, $16, $17)";

      if (!duckdb_utils::Prepare(conn_, prepare_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare media-source stats statement: "
                          << stmt.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error(
            "Failed to prepare media-source stats statement: " + stmt.error());
      }

      // パラメータをバインド
      int idx = 1;
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("id").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++,
                          get_string("trackIdentifier").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string("kind").c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("audioLevel"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("totalAudioEnergy"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("totalSamplesDuration"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("echoReturnLoss"));
      duckdb_bind_double(stmt.get_raw(), idx++,
                         get_double("echoReturnLossEnhancement"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("width"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("height"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64("frames"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double("framesPerSecond"));

      // 実行
      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert media-source stats: "
                          << exec_result.error()
                          << " for connection_id=" << connection_id;
        throw std::runtime_error("Failed to insert media-source stats: " +
                                 exec_result.error());
      }

    } else {
      RTC_LOG(LS_ERROR) << "Unsupported rtc_type: " << rtc_type
                        << " for connection_id=" << connection_id;
      return false;
    }

    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error writing RTC stats: " << e.what();
    return false;
  }
}

void DuckDBStatsWriter::Close() {
  std::lock_guard<std::mutex> lock(mutex_);

  // PreparedStatementを解放
  connections_stmt_.reset();
  codec_stats_stmt_.reset();
  inbound_rtp_stmt_.reset();
  outbound_rtp_stmt_.reset();
  media_source_stmt_.reset();

  if (conn_) {
    // 最後のチェックポイントを実行
    duckdb_utils::Result result;
    if (!duckdb_utils::ExecuteQuery(conn_, "PRAGMA wal_checkpoint(TRUNCATE)",
                                    result)) {
      RTC_LOG(LS_ERROR) << "Failed to execute WAL checkpoint on close: "
                        << result.error();
      // エラーが発生してもクローズ処理を続行
    }

    duckdb_disconnect(&conn_);
    conn_ = nullptr;
  }

  if (db_) {
    duckdb_close(&db_);
    db_ = nullptr;
  }

  initialized_ = false;
}

std::string DuckDBStatsWriter::GenerateFileName(const std::string& base_path) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            kMillisecondsPerSecond;

  std::tm tm;
  localtime_r(&time_t, &tm);

  std::ostringstream oss;
  oss << base_path << "/zakuro_stats_" << std::put_time(&tm, "%Y%m%d_%H%M%S")
      << "_" << std::setfill('0') << std::setw(kMillisecondFieldWidth)
      << ms.count() << ".ddb";

  return oss.str();
}

std::string DuckDBStatsWriter::ExecuteQuery(const std::string& sql) {
  if (!initialized_ || !conn_) {
    RTC_LOG(LS_ERROR)
        << "DuckDBStatsWriter::ExecuteQuery - database not initialized"
        << " query_length=" << sql.length();
    boost::json::object error;
    error["error"] = "Database not initialized";
    return boost::json::serialize(error);
  }

  std::lock_guard<std::mutex> lock(mutex_);

  duckdb_utils::Result result;
  if (!duckdb_utils::ExecuteQuery(conn_, sql, result)) {
    boost::json::object error;
    error["error"] = result.error();
    return boost::json::serialize(error);
  }

  // 結果をJSON形式に変換
  boost::json::object response;
  boost::json::array rows;

  // カラム情報を取得
  idx_t column_count = result.column_count();

  // 各行のデータを取得
  idx_t row_count = result.row_count();

  // 事前にメモリを予約してメモリ再割り当てを削減
  rows.reserve(row_count);

  for (idx_t row = 0; row < row_count; row++) {
    boost::json::object row_obj;

    for (idx_t col = 0; col < column_count; col++) {
      const char* col_name = duckdb_column_name(result.get(), col);

      // NULL値のチェック
      // JSONではnullptrとして表現し、クライアント側で適切に処理される
      if (duckdb_value_is_null(result.get(), col, row)) {
        row_obj[col_name] = nullptr;
        continue;
      }

      // 型に応じて値を取得
      duckdb_type col_type = duckdb_column_type(result.get(), col);
      switch (col_type) {
        case DUCKDB_TYPE_BOOLEAN:
          row_obj[col_name] = duckdb_value_boolean(result.get(), col, row);
          break;
        case DUCKDB_TYPE_TINYINT:
          row_obj[col_name] = duckdb_value_int8(result.get(), col, row);
          break;
        case DUCKDB_TYPE_SMALLINT:
          row_obj[col_name] = duckdb_value_int16(result.get(), col, row);
          break;
        case DUCKDB_TYPE_INTEGER:
          row_obj[col_name] = duckdb_value_int32(result.get(), col, row);
          break;
        case DUCKDB_TYPE_BIGINT:
          row_obj[col_name] =
              static_cast<int64_t>(duckdb_value_int64(result.get(), col, row));
          break;
        case DUCKDB_TYPE_FLOAT:
          row_obj[col_name] = duckdb_value_float(result.get(), col, row);
          break;
        case DUCKDB_TYPE_DOUBLE:
          row_obj[col_name] = duckdb_value_double(result.get(), col, row);
          break;
        case DUCKDB_TYPE_VARCHAR: {
          duckdb_utils::StringValue str_val(
              duckdb_value_varchar(result.get(), col, row));
          row_obj[col_name] = str_val.get();
          break;
        }
        case DUCKDB_TYPE_TIMESTAMP: {
          duckdb_utils::StringValue str_val(
              duckdb_value_varchar(result.get(), col, row));
          row_obj[col_name] = str_val.get();
          break;
        }
        default: {
          // その他の型は文字列として取得
          duckdb_utils::StringValue str_val(
              duckdb_value_varchar(result.get(), col, row));
          row_obj[col_name] = str_val.get();
          break;
        }
      }
    }

    rows.push_back(row_obj);
  }

  response["rows"] = rows;
  response["row_count"] = row_count;

  return boost::json::serialize(response);
}