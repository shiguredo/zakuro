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
  // シーケンスを作成
  auto seq_result = conn_->Query("CREATE SEQUENCE IF NOT EXISTS connections_pk_seq START 1");
  if (seq_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create sequence: " << seq_result->GetError();
    throw std::runtime_error("Failed to create sequence");
  }
  
  // 各統計情報テーブル用のシーケンスを作成
  auto codec_seq_result = conn_->Query("CREATE SEQUENCE IF NOT EXISTS codec_stats_pk_seq START 1");
  if (codec_seq_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create codec sequence: " << codec_seq_result->GetError();
    throw std::runtime_error("Failed to create codec sequence");
  }
  
  auto inbound_seq_result = conn_->Query("CREATE SEQUENCE IF NOT EXISTS inbound_rtp_stats_pk_seq START 1");
  if (inbound_seq_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create inbound sequence: " << inbound_seq_result->GetError();
    throw std::runtime_error("Failed to create inbound sequence");
  }
  
  auto outbound_seq_result = conn_->Query("CREATE SEQUENCE IF NOT EXISTS outbound_rtp_stats_pk_seq START 1");
  if (outbound_seq_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create outbound sequence: " << outbound_seq_result->GetError();
    throw std::runtime_error("Failed to create outbound sequence");
  }
  
  auto media_source_seq_result = conn_->Query("CREATE SEQUENCE IF NOT EXISTS media_source_stats_pk_seq START 1");
  if (media_source_seq_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create media source sequence: " << media_source_seq_result->GetError();
    throw std::runtime_error("Failed to create media source sequence");
  }
  
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
  
  auto result = conn_->Query(create_table_sql);
  if (result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create table: " << result->GetError();
    throw std::runtime_error("Failed to create table");
  }
  
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
  
  auto codec_result = conn_->Query(create_codec_table_sql);
  if (codec_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create codec_stats table: " << codec_result->GetError();
    throw std::runtime_error("Failed to create codec_stats table");
  }
  
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
  
  auto inbound_result = conn_->Query(create_inbound_table_sql);
  if (inbound_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create inbound_rtp_stats table: " << inbound_result->GetError();
    throw std::runtime_error("Failed to create inbound_rtp_stats table");
  }
  
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
  
  auto outbound_result = conn_->Query(create_outbound_table_sql);
  if (outbound_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create outbound_rtp_stats table: " << outbound_result->GetError();
    throw std::runtime_error("Failed to create outbound_rtp_stats table");
  }
  
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
  
  auto media_source_result = conn_->Query(create_media_source_table_sql);
  if (media_source_result->HasError()) {
    RTC_LOG(LS_ERROR) << "Failed to create media_source_stats table: " << media_source_result->GetError();
    throw std::runtime_error("Failed to create media_source_stats table");
  }
  
  // インデックスを作成
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_channel_id ON connections(channel_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_connection_id ON connections(connection_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_timestamp ON connections(timestamp)");
  
  // 各統計情報テーブルのインデックスを作成
  // codec_stats
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_codec_channel_id ON codec_stats(channel_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_codec_connection_id ON codec_stats(connection_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_codec_timestamp ON codec_stats(timestamp)");
  
  // inbound_rtp_stats
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_inbound_channel_id ON inbound_rtp_stats(channel_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_inbound_connection_id ON inbound_rtp_stats(connection_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_inbound_timestamp ON inbound_rtp_stats(timestamp)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_inbound_kind ON inbound_rtp_stats(kind)");
  
  // outbound_rtp_stats
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_outbound_channel_id ON outbound_rtp_stats(channel_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_outbound_connection_id ON outbound_rtp_stats(connection_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_outbound_timestamp ON outbound_rtp_stats(timestamp)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_outbound_kind ON outbound_rtp_stats(kind)");
  
  // media_source_stats
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_media_source_channel_id ON media_source_stats(channel_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_media_source_connection_id ON media_source_stats(connection_id)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_media_source_timestamp ON media_source_stats(timestamp)");
  conn_->Query("CREATE INDEX IF NOT EXISTS idx_media_source_kind ON media_source_stats(kind)");
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
      } else {
        inserted_count++;
      }
    }
    
    RTC_LOG(LS_INFO) << "Inserted " << inserted_count << " records to DuckDB";
    
    // コミット
    conn_->Query("COMMIT");
    
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error writing stats: " << e.what();
    conn_->Query("ROLLBACK");
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
      // NOT EXISTS を使用して、同じデータが存在しない場合のみ挿入
      auto prepared = conn_->Prepare(
          "INSERT INTO codec_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          "type, id, mime_type, payload_type, clock_rate, channels, sdp_fmtp_line) "
          "SELECT TO_TIMESTAMP($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12 "
          "WHERE NOT EXISTS ("
          "  SELECT 1 FROM codec_stats "
          "  WHERE connection_id = $4 "
          "  AND id = $7 "
          "  AND mime_type = $8 "
          "  AND payload_type = $9 "
          "  AND clock_rate = $10 "
          "  AND channels = $11 "
          "  AND sdp_fmtp_line = $12"
          ")");
      
      if (prepared->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to prepare codec statement: " << prepared->GetError();
        return;
      }
      
      auto result = prepared->Execute(
          timestamp,
          channel_id,
          session_id,
          connection_id,
          rtc_timestamp,
          get_string("type"),
          get_string("id"),
          get_string("mimeType"),
          get_int64("payloadType"),
          get_int64("clockRate"),
          get_int64("channels"),
          get_string("sdpFmtpLine")
      );
      
      if (result->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to insert codec stats: " << result->GetError();
      }
    } else if (rtc_type == "inbound-rtp") {
      auto prepared = conn_->Prepare(
          "INSERT INTO inbound_rtp_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          "type, id, ssrc, kind, transport_id, codec_id, packets_received, packets_lost, "
          "bytes_received, jitter, packets_received_with_ect1, packets_received_with_ce, "
          "packets_reported_as_lost, packets_reported_as_lost_but_recovered, "
          "last_packet_received_timestamp, header_bytes_received, "
          "packets_discarded, fec_bytes_received, fec_packets_received, fec_packets_discarded, nack_count, "
          "pli_count, fir_count, track_identifier, mid, remote_id, frames_decoded, key_frames_decoded, "
          "frames_rendered, frames_dropped, frame_width, frame_height, frames_per_second, qp_sum, "
          "total_decode_time, total_inter_frame_delay, total_squared_inter_frame_delay, pause_count, "
          "total_pauses_duration, freeze_count, total_freezes_duration, total_processing_delay, "
          "estimated_playout_timestamp, jitter_buffer_delay, jitter_buffer_target_delay, "
          "jitter_buffer_emitted_count, jitter_buffer_minimum_delay, total_samples_received, "
          "concealed_samples, silent_concealed_samples, concealment_events, "
          "inserted_samples_for_deceleration, removed_samples_for_acceleration, audio_level, "
          "total_audio_energy, total_samples_duration, frames_received, decoder_implementation, "
          "playout_id, power_efficient_decoder, frames_assembled_from_multiple_packets, "
          "total_assembly_time, retransmitted_packets_received, retransmitted_bytes_received, "
          "rtx_ssrc, fec_ssrc, total_corruption_probability, total_squared_corruption_probability, "
          "corruption_measurements) "
          "VALUES (TO_TIMESTAMP($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, "
          "$20, $21, $22, $23, $24, $25, $26, $27, $28, $29, $30, $31, $32, $33, $34, $35, $36, $37, "
          "$38, $39, $40, $41, $42, $43, $44, $45, $46, $47, $48, $49, $50, $51, $52, $53, $54, $55, "
          "$56, $57, $58, $59, $60, $61, $62, $63, $64, $65, $66, $67, $68, $69, $70, $71, $72, $73, "
          "$74, $75)");
      
      if (prepared->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to prepare inbound statement: " << prepared->GetError();
        return;
      }
      
      auto result = prepared->Execute(
          timestamp,
          channel_id,
          session_id,
          connection_id,
          rtc_timestamp,
          get_string("type"),
          get_string("id"),
          get_int64("ssrc"),
          get_string("kind"),
          get_string("transportId"),
          get_string("codecId"),
          get_int64("packetsReceived"),
          get_int64("packetsLost"),
          get_int64("bytesReceived"),
          get_double("jitter"),
          get_int64("packetsReceivedWithEct1"),
          get_int64("packetsReceivedWithCe"),
          get_int64("packetsReportedAsLost"),
          get_int64("packetsReportedAsLostButRecovered"),
          get_double("lastPacketReceivedTimestamp"),
          get_int64("headerBytesReceived"),
          get_int64("packetsDiscarded"),
          get_int64("fecBytesReceived"),
          get_int64("fecPacketsReceived"),
          get_int64("fecPacketsDiscarded"),
          get_int64("nackCount"),
          get_int64("pliCount"),
          get_int64("firCount"),
          get_string("trackIdentifier"),
          get_string("mid"),
          get_string("remoteId"),
          get_int64("framesDecoded"),
          get_int64("keyFramesDecoded"),
          get_int64("framesRendered"),
          get_int64("framesDropped"),
          get_int64("frameWidth"),
          get_int64("frameHeight"),
          get_double("framesPerSecond"),
          get_int64("qpSum"),
          get_double("totalDecodeTime"),
          get_double("totalInterFrameDelay"),
          get_double("totalSquaredInterFrameDelay"),
          get_int64("pauseCount"),
          get_double("totalPausesDuration"),
          get_int64("freezeCount"),
          get_double("totalFreezesDuration"),
          get_double("totalProcessingDelay"),
          get_double("estimatedPlayoutTimestamp"),
          get_double("jitterBufferDelay"),
          get_double("jitterBufferTargetDelay"),
          get_int64("jitterBufferEmittedCount"),
          get_double("jitterBufferMinimumDelay"),
          get_int64("totalSamplesReceived"),
          get_int64("concealedSamples"),
          get_int64("silentConcealedSamples"),
          get_int64("concealmentEvents"),
          get_int64("insertedSamplesForDeceleration"),
          get_int64("removedSamplesForAcceleration"),
          get_double("audioLevel"),
          get_double("totalAudioEnergy"),
          get_double("totalSamplesDuration"),
          get_int64("framesReceived"),
          get_string("decoderImplementation"),
          get_string("playoutId"),
          get_bool("powerEfficientDecoder"),
          get_int64("framesAssembledFromMultiplePackets"),
          get_double("totalAssemblyTime"),
          get_int64("retransmittedPacketsReceived"),
          get_int64("retransmittedBytesReceived"),
          get_int64("rtxSsrc"),
          get_int64("fecSsrc"),
          get_double("totalCorruptionProbability"),
          get_double("totalSquaredCorruptionProbability"),
          get_int64("corruptionMeasurements")
      );
      
      if (result->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to insert inbound stats: " << result->GetError();
      }
    } else if (rtc_type == "outbound-rtp") {
      // qualityLimitationDurationsの処理
      double qld_none = 0.0, qld_cpu = 0.0, qld_bandwidth = 0.0, qld_other = 0.0;
      if (json_obj.contains("qualityLimitationDurations") && json_obj.at("qualityLimitationDurations").is_object()) {
        auto qld = json_obj.at("qualityLimitationDurations").as_object();
        if (qld.contains("none") && qld.at("none").is_double()) qld_none = qld.at("none").as_double();
        if (qld.contains("cpu") && qld.at("cpu").is_double()) qld_cpu = qld.at("cpu").as_double();
        if (qld.contains("bandwidth") && qld.at("bandwidth").is_double()) qld_bandwidth = qld.at("bandwidth").as_double();
        if (qld.contains("other") && qld.at("other").is_double()) qld_other = qld.at("other").as_double();
      }
      
      auto prepared = conn_->Prepare(
          "INSERT INTO outbound_rtp_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          "type, id, ssrc, kind, transport_id, codec_id, packets_sent, bytes_sent, "
          "packets_sent_with_ect1, mid, media_source_id, remote_id, rid, encoding_index, "
          "header_bytes_sent, retransmitted_packets_sent, retransmitted_bytes_sent, "
          "rtx_ssrc, target_bitrate, total_encoded_bytes_target, frame_width, frame_height, "
          "frames_per_second, frames_sent, huge_frames_sent, frames_encoded, "
          "key_frames_encoded, qp_sum, total_encode_time, total_packet_send_delay, "
          "quality_limitation_reason, quality_limitation_duration_none, "
          "quality_limitation_duration_cpu, quality_limitation_duration_bandwidth, "
          "quality_limitation_duration_other, quality_limitation_resolution_changes, "
          "nack_count, pli_count, fir_count, encoder_implementation, "
          "power_efficient_encoder, active, scalability_mode) "
          "VALUES (TO_TIMESTAMP($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, "
          "$18, $19, $20, $21, $22, $23, $24, $25, $26, $27, $28, $29, $30, $31, $32, $33, "
          "$34, $35, $36, $37, $38, $39, $40, $41, $42, $43, $44, $45, $46, $47, $48)");
      
      if (prepared->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to prepare outbound statement: " << prepared->GetError();
        return;
      }
      
      auto result = prepared->Execute(
          timestamp,
          channel_id,
          session_id,
          connection_id,
          rtc_timestamp,
          get_string("type"),
          get_string("id"),
          get_int64("ssrc"),
          get_string("kind"),
          get_string("transportId"),
          get_string("codecId"),
          get_int64("packetsSent"),
          get_int64("bytesSent"),
          get_int64("packetsSentWithEct1"),
          get_string("mid"),
          get_string("mediaSourceId"),
          get_string("remoteId"),
          get_string("rid"),
          get_int64("encodingIndex"),
          get_int64("headerBytesSent"),
          get_int64("retransmittedPacketsSent"),
          get_int64("retransmittedBytesSent"),
          get_int64("rtxSsrc"),
          get_double("targetBitrate"),
          get_int64("totalEncodedBytesTarget"),
          get_int64("frameWidth"),
          get_int64("frameHeight"),
          get_double("framesPerSecond"),
          get_int64("framesSent"),
          get_int64("hugeFramesSent"),
          get_int64("framesEncoded"),
          get_int64("keyFramesEncoded"),
          get_int64("qpSum"),
          get_double("totalEncodeTime"),
          get_double("totalPacketSendDelay"),
          get_string("qualityLimitationReason"),
          qld_none,
          qld_cpu,
          qld_bandwidth,
          qld_other,
          get_int64("qualityLimitationResolutionChanges"),
          get_int64("nackCount"),
          get_int64("pliCount"),
          get_int64("firCount"),
          get_string("encoderImplementation"),
          get_bool("powerEfficientEncoder"),
          get_bool("active"),
          get_string("scalabilityMode")
      );
      
      if (result->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to insert outbound stats: " << result->GetError();
      }
    } else if (rtc_type == "media-source") {
      // media-sourceは常に挿入
      auto prepared = conn_->Prepare(
          "INSERT INTO media_source_stats (timestamp, channel_id, session_id, connection_id, rtc_timestamp, "
          "type, id, track_identifier, kind, audio_level, total_audio_energy, total_samples_duration, "
          "echo_return_loss, echo_return_loss_enhancement, width, height, frames, frames_per_second) "
          "VALUES (TO_TIMESTAMP($1), $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)");
      
      if (prepared->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to prepare media source statement: " << prepared->GetError();
        return;
      }
      
      // kindが存在しない場合はスキップ
      std::string kind = get_string("kind");
      if (kind.empty()) {
        RTC_LOG(LS_WARNING) << "Skipping media-source stats without kind field";
        return;
      }
      
      auto result = prepared->Execute(
          timestamp,
          channel_id,
          session_id,
          connection_id,
          rtc_timestamp,
          get_string("type"),
          get_string("id"),
          get_string("trackIdentifier"),
          kind,
          kind == "audio" ? duckdb::Value(get_double("audioLevel", 0.0)) : duckdb::Value(),
          kind == "audio" ? duckdb::Value(get_double("totalAudioEnergy", 0.0)) : duckdb::Value(),
          kind == "audio" ? duckdb::Value(get_double("totalSamplesDuration", 0.0)) : duckdb::Value(),
          kind == "audio" ? duckdb::Value(get_double("echoReturnLoss", 0.0)) : duckdb::Value(),
          kind == "audio" ? duckdb::Value(get_double("echoReturnLossEnhancement", 0.0)) : duckdb::Value(),
          kind == "video" ? duckdb::Value(get_int64("width", 0)) : duckdb::Value(),
          kind == "video" ? duckdb::Value(get_int64("height", 0)) : duckdb::Value(),
          kind == "video" ? duckdb::Value(get_int64("frames", 0)) : duckdb::Value(),
          kind == "video" ? duckdb::Value(get_double("framesPerSecond", 0.0)) : duckdb::Value()
      );
      
      if (result->HasError()) {
        RTC_LOG(LS_ERROR) << "Failed to insert media source stats: " << result->GetError();
      }
    } else {
      RTC_LOG(LS_WARNING) << "Unsupported rtc_type: " << rtc_type;
    }
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error writing RTC stats: " << e.what();
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