#include "duckdb_stats_writer.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <rtc_base/logging.h>
#include <boost/json.hpp>

DuckDBStatsWriter::DuckDBStatsWriter() = default;

DuckDBStatsWriter::~DuckDBStatsWriter() {
  Close();
}

bool DuckDBStatsWriter::Initialize(const std::string& base_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  RTC_LOG(LS_INFO) << "DuckDBStatsWriter::Initialize called with base_path: " << base_path;
  
  if (initialized_) {
    RTC_LOG(LS_INFO) << "DuckDBStatsWriter already initialized";
    return true;
  }
  
  // DuckDB library version を確認
  const char* lib_version = duckdb_library_version();
  RTC_LOG(LS_INFO) << "DuckDB library version: " << lib_version;
  
  std::string filename = GenerateFileName(base_path);
  db_filename_ = filename;  // ファイル名を保存
  RTC_LOG(LS_INFO) << "Creating DuckDB file: " << filename;
  
  // DuckDB インスタンスを作成
  char* error_message = nullptr;
  auto open_result = duckdb_open_ext(filename.c_str(), &db_, nullptr, &error_message);
  if (open_result == DuckDBError) {
    RTC_LOG(LS_ERROR) << "Failed to open DuckDB file: " << filename;
    if (error_message) {
      RTC_LOG(LS_ERROR) << "DuckDB error: " << error_message;
      duckdb_free(error_message);
    }
    return false;
  }
  RTC_LOG(LS_INFO) << "DuckDB opened successfully: " << filename;
  
  if (duckdb_connect(db_, &conn_) == DuckDBError) {
    RTC_LOG(LS_ERROR) << "Failed to create connection";
    duckdb_close(&db_);
    db_ = nullptr;
    return false;
  }
  RTC_LOG(LS_INFO) << "DuckDB connection created successfully";
  
  // WALモードはデフォルトで有効
  RTC_LOG(LS_INFO) << "DuckDB WAL mode enabled (default)";
  
  try {
    // テーブルを作成
    CreateTable();
    RTC_LOG(LS_INFO) << "Tables created successfully";
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to create tables: " << e.what();
    duckdb_disconnect(&conn_);
    conn_ = nullptr;
    duckdb_close(&db_);
    db_ = nullptr;
    return false;
  }
  
  initialized_ = true;
  RTC_LOG(LS_INFO) << "DuckDBStatsWriter initialization completed successfully, initialized_ = " << initialized_;
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
  execute_query("CREATE SEQUENCE IF NOT EXISTS inbound_rtp_stats_pk_seq START 1");
  execute_query("CREATE SEQUENCE IF NOT EXISTS outbound_rtp_stats_pk_seq START 1");
  execute_query("CREATE SEQUENCE IF NOT EXISTS media_source_stats_pk_seq START 1");
  
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
      sdp_fmtp_line VARCHAR
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
  execute_query("CREATE INDEX IF NOT EXISTS idx_channel_id ON connections(channel_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_connection_id ON connections(connection_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_timestamp ON connections(timestamp)");
  
  // 各統計情報テーブルのインデックスを作成
  execute_query("CREATE INDEX IF NOT EXISTS idx_codec_channel_id ON codec_stats(channel_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_codec_connection_id ON codec_stats(connection_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_codec_timestamp ON codec_stats(timestamp)");
  
  execute_query("CREATE INDEX IF NOT EXISTS idx_inbound_channel_id ON inbound_rtp_stats(channel_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_inbound_connection_id ON inbound_rtp_stats(connection_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_inbound_timestamp ON inbound_rtp_stats(timestamp)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_inbound_kind ON inbound_rtp_stats(kind)");
  
  execute_query("CREATE INDEX IF NOT EXISTS idx_outbound_channel_id ON outbound_rtp_stats(channel_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_outbound_connection_id ON outbound_rtp_stats(connection_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_outbound_timestamp ON outbound_rtp_stats(timestamp)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_outbound_kind ON outbound_rtp_stats(kind)");
  
  execute_query("CREATE INDEX IF NOT EXISTS idx_media_source_channel_id ON media_source_stats(channel_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_media_source_connection_id ON media_source_stats(connection_id)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_media_source_timestamp ON media_source_stats(timestamp)");
  execute_query("CREATE INDEX IF NOT EXISTS idx_media_source_kind ON media_source_stats(kind)");
}

void DuckDBStatsWriter::WriteStats(const std::vector<VirtualClientStats>& stats) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING) << "DuckDBStatsWriter not initialized";
    return;
  }
  
  RTC_LOG(LS_INFO) << "Writing " << stats.size() << " stats to DuckDB";
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  try {
    duckdb_utils::Transaction transaction(conn_);
    
    // プリペアドステートメントを準備
    duckdb_prepared_statement stmt = nullptr;
    const char* prepare_sql = "INSERT INTO connections (timestamp, channel_id, connection_id, session_id, audio, video, "
                             "websocket_connected, datachannel_connected) "
                             "VALUES (CURRENT_TIMESTAMP, $1, $2, $3, $4, $5, $6, $7)";
    
    if (duckdb_prepare(conn_, prepare_sql, &stmt) == DuckDBError) {
      const char* error = duckdb_prepare_error(stmt);
      RTC_LOG(LS_ERROR) << "Failed to prepare statement: " << error;
      duckdb_destroy_prepare(&stmt);
      throw std::runtime_error(std::string("Failed to prepare statement: ") + error);
    }
    
    // 各統計情報を挿入
    int inserted_count = 0;
    for (const auto& stat : stats) {
      // connection_id が空の場合はスキップ（まだ接続されていない）
      if (stat.connection_id.empty()) {
        RTC_LOG(LS_INFO) << "Skipping stats with empty connection_id"
                          << " channel_id=" << stat.channel_id
                          << " session_id=" << stat.session_id;
        continue;
      }
      
      RTC_LOG(LS_INFO) << "Inserting stats:"
                        << " channel_id=" << stat.channel_id
                        << " connection_id=" << stat.connection_id
                        << " session_id=" << stat.session_id
                        << " audio=" << stat.has_audio_track
                        << " video=" << stat.has_video_track;
      
      // パラメータをバインド
      duckdb_bind_varchar(stmt, 1, stat.channel_id.c_str());
      duckdb_bind_varchar(stmt, 2, stat.connection_id.c_str());
      duckdb_bind_varchar(stmt, 3, stat.session_id.c_str());
      duckdb_bind_boolean(stmt, 4, stat.has_audio_track);
      duckdb_bind_boolean(stmt, 5, stat.has_video_track);
      duckdb_bind_boolean(stmt, 6, stat.websocket_connected);
      duckdb_bind_boolean(stmt, 7, stat.datachannel_connected);
      
      // 実行
      duckdb_result exec_result;
      if (duckdb_execute_prepared(stmt, &exec_result) == DuckDBError) {
        RTC_LOG(LS_ERROR) << "Failed to insert stats: " << duckdb_result_error(&exec_result);
        duckdb_destroy_result(&exec_result);
      } else {
        inserted_count++;
        duckdb_destroy_result(&exec_result);
      }
    }
    
    duckdb_destroy_prepare(&stmt);
    
    RTC_LOG(LS_INFO) << "Inserted " << inserted_count << " records to DuckDB";
    
    // コミット
    transaction.Commit();
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error in WriteStats: " << e.what();
    // トランザクションは自動的にロールバックされる
  }
}

void DuckDBStatsWriter::WriteRTCStats(const std::string& channel_id,
                                     const std::string& session_id,
                                     const std::string& connection_id,
                                     const std::string& rtc_type,
                                     double rtc_timestamp,
                                     const std::string& rtc_data_json,
                                     double timestamp) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING) << "DuckDBStatsWriter not initialized";
    return;
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  try {
    duckdb_utils::Transaction transaction(conn_);
    
    // JSON文字列をパース
    auto json = boost::json::parse(rtc_data_json);
    auto json_obj = json.as_object();
    
    // 値を取得するヘルパー関数
    auto get_string = [&json_obj](const std::string& key, const std::string& default_val = "") -> std::string {
      if (json_obj.contains(key) && json_obj.at(key).is_string()) {
        return std::string(json_obj.at(key).as_string());
      }
      return default_val;
    };
    
    auto get_int64 = [&json_obj](const std::string& key, int64_t default_val = 0) -> int64_t {
      if (json_obj.contains(key)) {
        if (json_obj.at(key).is_int64()) return json_obj.at(key).as_int64();
        if (json_obj.at(key).is_uint64()) return static_cast<int64_t>(json_obj.at(key).as_uint64());
      }
      return default_val;
    };
    
    auto get_double = [&json_obj](const std::string& key, double default_val = 0.0) -> double {
      if (json_obj.contains(key)) {
        if (json_obj.at(key).is_double()) return json_obj.at(key).as_double();
        if (json_obj.at(key).is_int64()) return static_cast<double>(json_obj.at(key).as_int64());
        if (json_obj.at(key).is_uint64()) return static_cast<double>(json_obj.at(key).as_uint64());
      }
      return default_val;
    };
    
    auto get_bool = [&json_obj](const std::string& key, bool default_val = false) -> bool {
      if (json_obj.contains(key) && json_obj.at(key).is_bool()) {
        return json_obj.at(key).as_bool();
      }
      return default_val;
    };
    
    // rtc_typeに応じて適切なテーブルに挿入
    if (rtc_type == "codec") {
      // codec統計情報の挿入処理
      // C APIでは複雑なクエリのため、直接SQLを実行
      std::ostringstream sql;
      sql << "INSERT INTO codec_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          << "type, id, mime_type, payload_type, clock_rate, channels, sdp_fmtp_line) "
          << "SELECT TO_TIMESTAMP(" << timestamp << "), "
          << "'" << channel_id << "', "
          << "'" << session_id << "', "
          << "'" << connection_id << "', "
          << rtc_timestamp << ", "
          << "'" << get_string("type") << "', "
          << "'" << get_string("id") << "', "
          << "'" << get_string("mimeType") << "', "
          << get_int64("payloadType") << ", "
          << get_int64("clockRate") << ", "
          << get_int64("channels") << ", "
          << "'" << get_string("sdpFmtpLine") << "' "
          << "WHERE NOT EXISTS ("
          << "  SELECT 1 FROM codec_stats "
          << "  WHERE connection_id = '" << connection_id << "' "
          << "  AND id = '" << get_string("id") << "' "
          << "  AND mime_type = '" << get_string("mimeType") << "' "
          << "  AND payload_type = " << get_int64("payloadType") << " "
          << "  AND clock_rate = " << get_int64("clockRate") << " "
          << "  AND channels = " << get_int64("channels") << " "
          << "  AND sdp_fmtp_line = '" << get_string("sdpFmtpLine") << "'"
          << ")";
      
      duckdb_result result;
      if (duckdb_query(conn_, sql.str().c_str(), &result) == DuckDBError) {
        RTC_LOG(LS_ERROR) << "Failed to insert codec stats: " << duckdb_result_error(&result);
      }
      duckdb_destroy_result(&result);
      
    } else if (rtc_type == "inbound-rtp") {
      // inbound-rtp統計情報の挿入処理
      std::ostringstream sql;
      sql << "INSERT INTO inbound_rtp_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          << "type, id, ssrc, kind, transport_id, codec_id, "
          << "packets_received, packets_lost, bytes_received, jitter, "
          << "last_packet_received_timestamp, header_bytes_received, packets_discarded, "
          << "fec_bytes_received, fec_packets_received, fec_packets_discarded, "
          << "nack_count, pli_count, fir_count, track_identifier, mid, remote_id, "
          << "frames_decoded, key_frames_decoded, frames_rendered, frames_dropped, "
          << "frame_width, frame_height, frames_per_second, qp_sum, "
          << "total_decode_time, total_inter_frame_delay, total_squared_inter_frame_delay, "
          << "pause_count, total_pauses_duration, freeze_count, total_freezes_duration, "
          << "total_processing_delay, estimated_playout_timestamp, jitter_buffer_delay, "
          << "jitter_buffer_target_delay, jitter_buffer_emitted_count, jitter_buffer_minimum_delay, "
          << "total_samples_received, concealed_samples, silent_concealed_samples, concealment_events, "
          << "inserted_samples_for_deceleration, removed_samples_for_acceleration, "
          << "audio_level, total_audio_energy, total_samples_duration, frames_received, "
          << "decoder_implementation, playout_id, power_efficient_decoder, "
          << "frames_assembled_from_multiple_packets, total_assembly_time, "
          << "retransmitted_packets_received, retransmitted_bytes_received, "
          << "rtx_ssrc, fec_ssrc) "
          << "VALUES (CURRENT_TIMESTAMP, "
          << "'" << channel_id << "', "
          << "'" << session_id << "', "
          << "'" << connection_id << "', "
          << rtc_timestamp << ", "
          << "'" << get_string("type") << "', "
          << "'" << get_string("id") << "', "
          << get_int64("ssrc") << ", "
          << "'" << get_string("kind") << "', "
          << "'" << get_string("transportId") << "', "
          << "'" << get_string("codecId") << "', "
          << get_int64("packetsReceived") << ", "
          << get_int64("packetsLost") << ", "
          << get_int64("bytesReceived") << ", "
          << get_double("jitter") << ", "
          << get_double("lastPacketReceivedTimestamp") << ", "
          << get_int64("headerBytesReceived") << ", "
          << get_int64("packetsDiscarded") << ", "
          << get_int64("fecBytesReceived") << ", "
          << get_int64("fecPacketsReceived") << ", "
          << get_int64("fecPacketsDiscarded") << ", "
          << get_int64("nackCount") << ", "
          << get_int64("pliCount") << ", "
          << get_int64("firCount") << ", "
          << "'" << get_string("trackIdentifier") << "', "
          << "'" << get_string("mid") << "', "
          << "'" << get_string("remoteId") << "', "
          << get_int64("framesDecoded") << ", "
          << get_int64("keyFramesDecoded") << ", "
          << get_int64("framesRendered") << ", "
          << get_int64("framesDropped") << ", "
          << get_int64("frameWidth") << ", "
          << get_int64("frameHeight") << ", "
          << get_double("framesPerSecond") << ", "
          << get_int64("qpSum") << ", "
          << get_double("totalDecodeTime") << ", "
          << get_double("totalInterFrameDelay") << ", "
          << get_double("totalSquaredInterFrameDelay") << ", "
          << get_int64("pauseCount") << ", "
          << get_double("totalPausesDuration") << ", "
          << get_int64("freezeCount") << ", "
          << get_double("totalFreezesDuration") << ", "
          << get_double("totalProcessingDelay") << ", "
          << get_double("estimatedPlayoutTimestamp") << ", "
          << get_double("jitterBufferDelay") << ", "
          << get_double("jitterBufferTargetDelay") << ", "
          << get_int64("jitterBufferEmittedCount") << ", "
          << get_double("jitterBufferMinimumDelay") << ", "
          << get_int64("totalSamplesReceived") << ", "
          << get_int64("concealedSamples") << ", "
          << get_int64("silentConcealedSamples") << ", "
          << get_int64("concealmentEvents") << ", "
          << get_int64("insertedSamplesForDeceleration") << ", "
          << get_int64("removedSamplesForAcceleration") << ", "
          << get_double("audioLevel") << ", "
          << get_double("totalAudioEnergy") << ", "
          << get_double("totalSamplesDuration") << ", "
          << get_int64("framesReceived") << ", "
          << "'" << get_string("decoderImplementation") << "', "
          << "'" << get_string("playoutId") << "', "
          << (get_bool("powerEfficientDecoder") ? "true" : "false") << ", "
          << get_int64("framesAssembledFromMultiplePackets") << ", "
          << get_double("totalAssemblyTime") << ", "
          << get_int64("retransmittedPacketsReceived") << ", "
          << get_int64("retransmittedBytesReceived") << ", "
          << get_int64("rtxSsrc") << ", "
          << get_int64("fecSsrc") << ")";
      
      duckdb_result result;
      if (duckdb_query(conn_, sql.str().c_str(), &result) == DuckDBError) {
        RTC_LOG(LS_ERROR) << "Failed to insert inbound-rtp stats: " << duckdb_result_error(&result);
        RTC_LOG(LS_ERROR) << "SQL: " << sql.str();
      } else {
        RTC_LOG(LS_INFO) << "Successfully inserted inbound-rtp stats for connection: " << connection_id;
      }
      duckdb_destroy_result(&result);
      
    } else if (rtc_type == "outbound-rtp") {
      // outbound-rtp統計情報の挿入処理
      std::ostringstream sql;
      sql << "INSERT INTO outbound_rtp_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          << "type, id, ssrc, kind, transport_id, codec_id, "
          << "packets_sent, bytes_sent, packets_sent_with_ect1, "
          << "mid, media_source_id, remote_id, rid, encoding_index, "
          << "header_bytes_sent, retransmitted_packets_sent, retransmitted_bytes_sent, "
          << "rtx_ssrc, target_bitrate, total_encoded_bytes_target, "
          << "frame_width, frame_height, frames_per_second, frames_sent, "
          << "huge_frames_sent, frames_encoded, key_frames_encoded, qp_sum, "
          << "total_encode_time, total_packet_send_delay, "
          << "quality_limitation_reason, quality_limitation_duration_none, "
          << "quality_limitation_duration_cpu, quality_limitation_duration_bandwidth, "
          << "quality_limitation_duration_other, quality_limitation_resolution_changes, "
          << "nack_count, pli_count, fir_count, encoder_implementation, "
          << "power_efficient_encoder, active, scalability_mode) "
          << "VALUES (CURRENT_TIMESTAMP, "
          << "'" << channel_id << "', "
          << "'" << session_id << "', "
          << "'" << connection_id << "', "
          << rtc_timestamp << ", "
          << "'" << get_string("type") << "', "
          << "'" << get_string("id") << "', "
          << get_int64("ssrc") << ", "
          << "'" << get_string("kind") << "', "
          << "'" << get_string("transportId") << "', "
          << "'" << get_string("codecId") << "', "
          << get_int64("packetsSent") << ", "
          << get_int64("bytesSent") << ", "
          << get_int64("packetsSentWithEct1") << ", "
          << "'" << get_string("mid") << "', "
          << "'" << get_string("mediaSourceId") << "', "
          << "'" << get_string("remoteId") << "', "
          << "'" << get_string("rid") << "', "
          << get_int64("encodingIndex") << ", "
          << get_int64("headerBytesSent") << ", "
          << get_int64("retransmittedPacketsSent") << ", "
          << get_int64("retransmittedBytesSent") << ", "
          << get_int64("rtxSsrc") << ", "
          << get_double("targetBitrate") << ", "
          << get_int64("totalEncodedBytesTarget") << ", "
          << get_int64("frameWidth") << ", "
          << get_int64("frameHeight") << ", "
          << get_double("framesPerSecond") << ", "
          << get_int64("framesSent") << ", "
          << get_int64("hugeFramesSent") << ", "
          << get_int64("framesEncoded") << ", "
          << get_int64("keyFramesEncoded") << ", "
          << get_int64("qpSum") << ", "
          << get_double("totalEncodeTime") << ", "
          << get_double("totalPacketSendDelay") << ", "
          << "'" << get_string("qualityLimitationReason") << "', "
          << get_double("qualityLimitationDurationNone") << ", "
          << get_double("qualityLimitationDurationCpu") << ", "
          << get_double("qualityLimitationDurationBandwidth") << ", "
          << get_double("qualityLimitationDurationOther") << ", "
          << get_int64("qualityLimitationResolutionChanges") << ", "
          << get_int64("nackCount") << ", "
          << get_int64("pliCount") << ", "
          << get_int64("firCount") << ", "
          << "'" << get_string("encoderImplementation") << "', "
          << (get_bool("powerEfficientEncoder") ? "true" : "false") << ", "
          << (get_bool("active") ? "true" : "false") << ", "
          << "'" << get_string("scalabilityMode") << "')";
      
      duckdb_result result;
      if (duckdb_query(conn_, sql.str().c_str(), &result) == DuckDBError) {
        RTC_LOG(LS_ERROR) << "Failed to insert outbound-rtp stats: " << duckdb_result_error(&result);
      } else {
        RTC_LOG(LS_INFO) << "Successfully inserted outbound-rtp stats for connection: " << connection_id;
      }
      duckdb_destroy_result(&result);
      
    } else if (rtc_type == "media-source") {
      // media-source統計情報の挿入処理
      std::ostringstream sql;
      sql << "INSERT INTO media_source_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          << "type, id, track_identifier, kind, "
          << "audio_level, total_audio_energy, total_samples_duration, "
          << "echo_return_loss, echo_return_loss_enhancement, "
          << "width, height, frames, frames_per_second) "
          << "VALUES (CURRENT_TIMESTAMP, "
          << "'" << channel_id << "', "
          << "'" << session_id << "', "
          << "'" << connection_id << "', "
          << rtc_timestamp << ", "
          << "'" << get_string("type") << "', "
          << "'" << get_string("id") << "', "
          << "'" << get_string("trackIdentifier") << "', "
          << "'" << get_string("kind") << "', "
          << get_double("audioLevel") << ", "
          << get_double("totalAudioEnergy") << ", "
          << get_double("totalSamplesDuration") << ", "
          << get_double("echoReturnLoss") << ", "
          << get_double("echoReturnLossEnhancement") << ", "
          << get_int64("width") << ", "
          << get_int64("height") << ", "
          << get_int64("frames") << ", "
          << get_double("framesPerSecond") << ")";
      
      duckdb_result result;
      if (duckdb_query(conn_, sql.str().c_str(), &result) == DuckDBError) {
        RTC_LOG(LS_ERROR) << "Failed to insert media-source stats: " << duckdb_result_error(&result);
      } else {
        RTC_LOG(LS_INFO) << "Successfully inserted media-source stats for connection: " << connection_id;
      }
      duckdb_destroy_result(&result);
      
    } else {
      RTC_LOG(LS_WARNING) << "Unsupported rtc_type: " << rtc_type;
    }
    
    // トランザクションをコミット
    transaction.Commit();
    RTC_LOG(LS_INFO) << "Transaction committed successfully for rtc_type: " << rtc_type;
    
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error writing RTC stats: " << e.what();
    // トランザクションは自動的にロールバックされる
  }
}

void DuckDBStatsWriter::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (conn_) {
    // 最後のチェックポイントを実行
    duckdb_result result;
    duckdb_query(conn_, "PRAGMA wal_checkpoint(TRUNCATE)", &result);
    duckdb_destroy_result(&result);
    
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
      now.time_since_epoch()) % 1000;
  
  std::tm tm;
  localtime_r(&time_t, &tm);
  
  std::ostringstream oss;
  oss << base_path << "/zakuro_stats_"
      << std::put_time(&tm, "%Y%m%d_%H%M%S")
      << "_" << std::setfill('0') << std::setw(3) << ms.count()
      << ".ddb";
  
  return oss.str();
}

std::string DuckDBStatsWriter::ExecuteQuery(const std::string& sql) {
  if (!initialized_ || !conn_) {
    RTC_LOG(LS_ERROR) << "Database not initialized in ExecuteQuery";
    boost::json::object error;
    error["error"] = "Database not initialized";
    return boost::json::serialize(error);
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  // WALのデータを確実に読み取るため、チェックポイントを実行
  duckdb_result checkpoint_result;
  if (duckdb_query(conn_, "CHECKPOINT", &checkpoint_result) == DuckDBError) {
    RTC_LOG(LS_WARNING) << "Failed to execute checkpoint: " << duckdb_result_error(&checkpoint_result);
  }
  duckdb_destroy_result(&checkpoint_result);
  
  duckdb_result result;
  memset(&result, 0, sizeof(duckdb_result));  // 結果を初期化
  
  if (duckdb_query(conn_, sql.c_str(), &result) == DuckDBError) {
    boost::json::object error;
    error["error"] = duckdb_result_error(&result);
    duckdb_destroy_result(&result);
    return boost::json::serialize(error);
  }
  
  // 結果をJSON形式に変換
  boost::json::object response;
  boost::json::array rows;
  
  // カラム情報を取得
  idx_t column_count = duckdb_column_count(&result);
  
  // 各行のデータを取得
  idx_t row_count = duckdb_row_count(&result);
  
  for (idx_t row = 0; row < row_count; row++) {
    boost::json::object row_obj;
    
    for (idx_t col = 0; col < column_count; col++) {
      const char* col_name = duckdb_column_name(&result, col);
      
      // NULL値のチェック
      if (duckdb_value_is_null(&result, col, row)) {
        row_obj[col_name] = nullptr;
        continue;
      }
      
      // 型に応じて値を取得
      duckdb_type col_type = duckdb_column_type(&result, col);
      switch (col_type) {
        case DUCKDB_TYPE_BOOLEAN:
          row_obj[col_name] = duckdb_value_boolean(&result, col, row);
          break;
        case DUCKDB_TYPE_TINYINT:
          row_obj[col_name] = duckdb_value_int8(&result, col, row);
          break;
        case DUCKDB_TYPE_SMALLINT:
          row_obj[col_name] = duckdb_value_int16(&result, col, row);
          break;
        case DUCKDB_TYPE_INTEGER:
          row_obj[col_name] = duckdb_value_int32(&result, col, row);
          break;
        case DUCKDB_TYPE_BIGINT:
          row_obj[col_name] = static_cast<int64_t>(duckdb_value_int64(&result, col, row));
          break;
        case DUCKDB_TYPE_FLOAT:
          row_obj[col_name] = duckdb_value_float(&result, col, row);
          break;
        case DUCKDB_TYPE_DOUBLE:
          row_obj[col_name] = duckdb_value_double(&result, col, row);
          break;
        case DUCKDB_TYPE_VARCHAR: {
          duckdb_utils::StringValue str_val(duckdb_value_varchar(&result, col, row));
          row_obj[col_name] = str_val.get();
          break;
        }
        case DUCKDB_TYPE_TIMESTAMP: {
          duckdb_utils::StringValue str_val(duckdb_value_varchar(&result, col, row));
          row_obj[col_name] = str_val.get();
          break;
        }
        default: {
          // その他の型は文字列として取得
          duckdb_utils::StringValue str_val(duckdb_value_varchar(&result, col, row));
          row_obj[col_name] = str_val.get();
          break;
        }
      }
    }
    
    rows.push_back(row_obj);
  }
  
  response["rows"] = rows;
  response["row_count"] = row_count;
  
  duckdb_destroy_result(&result);
  
  return boost::json::serialize(response);
}