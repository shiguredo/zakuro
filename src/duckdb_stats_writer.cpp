#include "duckdb_stats_writer.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include <rtc_base/logging.h>
#include <boost/json.hpp>

#include "virtual_client.h"
#include "zakuro_version.h"

namespace {

// ファイル名生成時の定数
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMillisecondFieldWidth = 3;

// DEPS ファイルからバージョン情報を読み込む
std::unordered_map<std::string, std::string> ReadDepsFile() {
  std::unordered_map<std::string, std::string> versions;
  std::ifstream file("DEPS");

  if (!file.is_open()) {
    RTC_LOG(LS_WARNING) << "Failed to open DEPS file";
    return versions;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t pos = line.find('=');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1);
      versions[key] = value;
    }
  }

  return versions;
}

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

  std::string filename = GenerateFileName(base_path);
  db_filename_ = filename;
  RTC_LOG(LS_INFO) << "DuckDB file: " << filename;

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

  try {
    CreateTables();
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to create tables: " << e.what();
    CleanupResources();
    return false;
  }

  initialized_ = true;
  return true;
}

void DuckDBStatsWriter::CreateTables() {
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
  execute_query("CREATE SEQUENCE IF NOT EXISTS connection_pk_seq START 1");
  execute_query("CREATE SEQUENCE IF NOT EXISTS rtc_stats_codec_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS rtc_stats_inbound_rtp_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS rtc_stats_outbound_rtp_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS rtc_stats_media_source_pk_seq START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS rtc_stats_remote_inbound_rtp_pk_seq START "
      "1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS rtc_stats_remote_outbound_rtp_pk_seq "
      "START 1");
  execute_query(
      "CREATE SEQUENCE IF NOT EXISTS rtc_stats_data_channel_pk_seq START 1");

  // zakuro テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS zakuro (
      version VARCHAR,
      environment VARCHAR,
      webrtc_version VARCHAR,
      sora_cpp_sdk_version VARCHAR,
      boost_version VARCHAR,
      cli11_version VARCHAR,
      cmake_version VARCHAR,
      blend2d_version VARCHAR,
      openh264_version VARCHAR,
      duckdb_version VARCHAR,
      config_mode VARCHAR,
      config_json JSON,
      start_timestamp TIMESTAMP,
      stop_timestamp TIMESTAMP
    )
  )");

  // connection テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS connection (
      pk BIGINT PRIMARY KEY DEFAULT nextval('connection_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      connection_id VARCHAR,
      session_id VARCHAR,
      role VARCHAR,
      audio BOOLEAN,
      video BOOLEAN,
      websocket_connected BOOLEAN,
      datachannel_connected BOOLEAN
    )
  )");

  // rtc_stats_codec テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_codec (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_codec_pk_seq'),
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
  )");

  // rtc_stats_inbound_rtp テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_inbound_rtp (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_inbound_rtp_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      ssrc BIGINT,
      kind VARCHAR,
      transport_id VARCHAR,
      codec_id VARCHAR,
      packets_received BIGINT,
      packets_lost BIGINT,
      bytes_received BIGINT,
      jitter DOUBLE,
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
      fec_ssrc BIGINT
    )
  )");

  // rtc_stats_outbound_rtp テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_outbound_rtp (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_outbound_rtp_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      ssrc BIGINT,
      kind VARCHAR,
      transport_id VARCHAR,
      codec_id VARCHAR,
      packets_sent BIGINT,
      bytes_sent BIGINT,
      mid VARCHAR,
      media_source_id VARCHAR,
      remote_id VARCHAR,
      rid VARCHAR,
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
      total_encode_time DOUBLE,
      total_packet_send_delay DOUBLE,
      quality_limitation_reason VARCHAR,
      quality_limitation_resolution_changes BIGINT,
      nack_count BIGINT,
      pli_count BIGINT,
      fir_count BIGINT,
      encoder_implementation VARCHAR,
      power_efficient_encoder BOOLEAN,
      active BOOLEAN,
      scalability_mode VARCHAR
    )
  )");

  // rtc_stats_media_source テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_media_source (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_media_source_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      track_identifier VARCHAR,
      kind VARCHAR,
      audio_level DOUBLE,
      total_audio_energy DOUBLE,
      total_samples_duration DOUBLE,
      echo_return_loss DOUBLE,
      echo_return_loss_enhancement DOUBLE,
      width BIGINT,
      height BIGINT,
      frames BIGINT,
      frames_per_second DOUBLE
    )
  )");

  // rtc_stats_remote_inbound_rtp テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_remote_inbound_rtp (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_remote_inbound_rtp_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      ssrc BIGINT,
      kind VARCHAR,
      transport_id VARCHAR,
      codec_id VARCHAR,
      packets_received BIGINT,
      packets_lost BIGINT,
      jitter DOUBLE,
      local_id VARCHAR,
      round_trip_time DOUBLE,
      total_round_trip_time DOUBLE,
      fraction_lost DOUBLE,
      round_trip_time_measurements BIGINT
    )
  )");

  // rtc_stats_remote_outbound_rtp テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_remote_outbound_rtp (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_remote_outbound_rtp_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      ssrc BIGINT,
      kind VARCHAR,
      transport_id VARCHAR,
      codec_id VARCHAR,
      packets_sent BIGINT,
      bytes_sent BIGINT,
      local_id VARCHAR,
      remote_timestamp DOUBLE,
      reports_sent BIGINT,
      round_trip_time DOUBLE,
      total_round_trip_time DOUBLE,
      round_trip_time_measurements BIGINT
    )
  )");

  // rtc_stats_data_channel テーブル
  execute_query(R"(
    CREATE TABLE IF NOT EXISTS rtc_stats_data_channel (
      pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_data_channel_pk_seq'),
      timestamp TIMESTAMP,
      channel_id VARCHAR,
      session_id VARCHAR,
      connection_id VARCHAR,
      rtc_timestamp DOUBLE,
      type VARCHAR,
      id VARCHAR,
      label VARCHAR,
      protocol VARCHAR,
      data_channel_identifier SMALLINT,
      state VARCHAR,
      messages_sent BIGINT,
      bytes_sent BIGINT,
      messages_received BIGINT,
      bytes_received BIGINT
    )
  )");

  // インデックスを作成
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_connection_id ON "
      "connection(connection_id)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_connection_composite ON "
      "connection(channel_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_codec_composite ON "
      "rtc_stats_codec(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_inbound_rtp_composite ON "
      "rtc_stats_inbound_rtp(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_outbound_rtp_composite ON "
      "rtc_stats_outbound_rtp(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_media_source_composite ON "
      "rtc_stats_media_source(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_remote_inbound_rtp_composite "
      "ON rtc_stats_remote_inbound_rtp(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_remote_outbound_rtp_composite "
      "ON rtc_stats_remote_outbound_rtp(channel_id, connection_id, timestamp)");
  execute_query(
      "CREATE INDEX IF NOT EXISTS idx_rtc_stats_data_channel_composite ON "
      "rtc_stats_data_channel(channel_id, connection_id, timestamp)");
}

std::string DuckDBStatsWriter::GenerateFileName(const std::string& base_path) {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            kMillisecondsPerSecond;

  std::tm tm_now;
  localtime_r(&time_t_now, &tm_now);

  std::ostringstream oss;
  oss << base_path << "/zakuro_" << std::put_time(&tm_now, "%Y%m%d_%H%M%S")
      << "_" << std::setfill('0') << std::setw(kMillisecondFieldWidth)
      << ms.count() << ".db";

  return oss.str();
}

void DuckDBStatsWriter::CleanupResources() {
  if (conn_) {
    duckdb_disconnect(&conn_);
    conn_ = nullptr;
  }
  if (db_) {
    duckdb_close(&db_);
    db_ = nullptr;
  }
  initialized_ = false;
}

void DuckDBStatsWriter::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  CleanupResources();
}

bool DuckDBStatsWriter::WriteConnectionStats(
    const std::vector<VirtualClientStats>& stats) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING)
        << "DuckDBStatsWriter::WriteConnectionStats - not initialized";
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  try {
    duckdb_utils::Transaction transaction(conn_);

    duckdb_utils::PreparedStatement stmt;
    const char* sql =
        "INSERT INTO connection (timestamp, channel_id, connection_id, "
        "session_id, role, audio, video, websocket_connected, "
        "datachannel_connected) "
        "VALUES (CURRENT_TIMESTAMP, $channel_id, $connection_id, $session_id, "
        "$role, $audio, $video, $websocket_connected, $datachannel_connected)";
    if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
      RTC_LOG(LS_ERROR) << "Failed to prepare statement: " << stmt.error();
      throw std::runtime_error("Failed to prepare statement: " + stmt.error());
    }

    for (const auto& stat : stats) {
      if (stat.connection_id.empty()) {
        continue;
      }

      duckdb_clear_bindings(stmt.get_raw());

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", stat.channel_id.c_str());
      binder.BindVarchar("connection_id", stat.connection_id.c_str());
      binder.BindVarchar("session_id", stat.session_id.c_str());
      binder.BindVarchar("role", stat.role.c_str());
      binder.BindBoolean("audio", stat.has_audio_track);
      binder.BindBoolean("video", stat.has_video_track);
      binder.BindBoolean("websocket_connected", stat.websocket_connected);
      binder.BindBoolean("datachannel_connected", stat.datachannel_connected);

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert stats for connection_id="
                          << stat.connection_id << ": " << exec_result.error();
        throw std::runtime_error("Failed to insert stats: " +
                                 exec_result.error());
      }
    }

    transaction.Commit();
    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error in WriteConnectionStats: " << e.what();
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
    boost::json::value json;
    try {
      json = boost::json::parse(rtc_data_json);
    } catch (const std::exception& e) {
      RTC_LOG(LS_ERROR) << "Failed to parse JSON: " << e.what()
                        << " for connection_id=" << connection_id
                        << " rtc_type=" << rtc_type;
      return false;
    }
    auto json_obj = json.as_object();

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

    if (rtc_type == "codec") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_codec (timestamp, channel_id, session_id, "
          "connection_id, rtc_timestamp, type, id, mime_type, payload_type, "
          "clock_rate, channels, sdp_fmtp_line) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, "
          "$rtc_timestamp, $type, $id, $mime_type, $payload_type, $clock_rate, "
          "$channels, $sdp_fmtp_line) "
          "ON CONFLICT (connection_id, id, mime_type, payload_type, "
          "clock_rate, channels, sdp_fmtp_line) DO NOTHING";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare codec statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindVarchar("mime_type", get_string("mimeType").c_str());
      binder.BindInt64("payload_type", get_int64("payloadType"));
      binder.BindInt64("clock_rate", get_int64("clockRate"));
      binder.BindInt64("channels", get_int64("channels"));
      binder.BindVarchar("sdp_fmtp_line", get_string("sdpFmtpLine").c_str());

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert codec stats: "
                          << exec_result.error();
        return false;
      }

    } else if (rtc_type == "inbound-rtp") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_inbound_rtp (timestamp, channel_id, "
          "session_id, connection_id, rtc_timestamp, type, id, ssrc, kind, "
          "transport_id, codec_id, packets_received, packets_lost, "
          "bytes_received, jitter, last_packet_received_timestamp, "
          "header_bytes_received, packets_discarded, fec_bytes_received, "
          "fec_packets_received, fec_packets_discarded, nack_count, pli_count, "
          "fir_count, track_identifier, mid, remote_id, frames_decoded, "
          "key_frames_decoded, frames_rendered, frames_dropped, frame_width, "
          "frame_height, frames_per_second, qp_sum, total_decode_time, "
          "total_inter_frame_delay, total_squared_inter_frame_delay, "
          "pause_count, total_pauses_duration, freeze_count, "
          "total_freezes_duration, total_processing_delay, "
          "estimated_playout_timestamp, jitter_buffer_delay, "
          "jitter_buffer_target_delay, jitter_buffer_emitted_count, "
          "jitter_buffer_minimum_delay, total_samples_received, "
          "concealed_samples, silent_concealed_samples, concealment_events, "
          "inserted_samples_for_deceleration, "
          "removed_samples_for_acceleration, audio_level, total_audio_energy, "
          "total_samples_duration, frames_received, decoder_implementation, "
          "playout_id, power_efficient_decoder, "
          "frames_assembled_from_multiple_packets, total_assembly_time, "
          "retransmitted_packets_received, retransmitted_bytes_received, "
          "rtx_ssrc, fec_ssrc) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, $rtc_timestamp, $type, $id, $ssrc, $kind, "
          "$transport_id, $codec_id, $packets_received, $packets_lost, "
          "$bytes_received, $jitter, $last_packet_received_timestamp, "
          "$header_bytes_received, $packets_discarded, $fec_bytes_received, "
          "$fec_packets_received, $fec_packets_discarded, $nack_count, "
          "$pli_count, $fir_count, $track_identifier, $mid, $remote_id, "
          "$frames_decoded, $key_frames_decoded, $frames_rendered, "
          "$frames_dropped, $frame_width, $frame_height, $frames_per_second, "
          "$qp_sum, $total_decode_time, $total_inter_frame_delay, "
          "$total_squared_inter_frame_delay, $pause_count, "
          "$total_pauses_duration, $freeze_count, $total_freezes_duration, "
          "$total_processing_delay, $estimated_playout_timestamp, "
          "$jitter_buffer_delay, $jitter_buffer_target_delay, "
          "$jitter_buffer_emitted_count, $jitter_buffer_minimum_delay, "
          "$total_samples_received, $concealed_samples, "
          "$silent_concealed_samples, $concealment_events, "
          "$inserted_samples_for_deceleration, "
          "$removed_samples_for_acceleration, $audio_level, "
          "$total_audio_energy, $total_samples_duration, $frames_received, "
          "$decoder_implementation, $playout_id, $power_efficient_decoder, "
          "$frames_assembled_from_multiple_packets, $total_assembly_time, "
          "$retransmitted_packets_received, $retransmitted_bytes_received, "
          "$rtx_ssrc, $fec_ssrc)";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare inbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindInt64("ssrc", get_int64("ssrc"));
      binder.BindVarchar("kind", get_string("kind").c_str());
      binder.BindVarchar("transport_id", get_string("transportId").c_str());
      binder.BindVarchar("codec_id", get_string("codecId").c_str());
      binder.BindInt64("packets_received", get_int64("packetsReceived"));
      binder.BindInt64("packets_lost", get_int64("packetsLost"));
      binder.BindInt64("bytes_received", get_int64("bytesReceived"));
      binder.BindDouble("jitter", get_double("jitter"));
      binder.BindDouble("last_packet_received_timestamp",
                        get_double("lastPacketReceivedTimestamp"));
      binder.BindInt64("header_bytes_received",
                       get_int64("headerBytesReceived"));
      binder.BindInt64("packets_discarded", get_int64("packetsDiscarded"));
      binder.BindInt64("fec_bytes_received", get_int64("fecBytesReceived"));
      binder.BindInt64("fec_packets_received", get_int64("fecPacketsReceived"));
      binder.BindInt64("fec_packets_discarded",
                       get_int64("fecPacketsDiscarded"));
      binder.BindInt64("nack_count", get_int64("nackCount"));
      binder.BindInt64("pli_count", get_int64("pliCount"));
      binder.BindInt64("fir_count", get_int64("firCount"));
      binder.BindVarchar("track_identifier",
                         get_string("trackIdentifier").c_str());
      binder.BindVarchar("mid", get_string("mid").c_str());
      binder.BindVarchar("remote_id", get_string("remoteId").c_str());
      binder.BindInt64("frames_decoded", get_int64("framesDecoded"));
      binder.BindInt64("key_frames_decoded", get_int64("keyFramesDecoded"));
      binder.BindInt64("frames_rendered", get_int64("framesRendered"));
      binder.BindInt64("frames_dropped", get_int64("framesDropped"));
      binder.BindInt64("frame_width", get_int64("frameWidth"));
      binder.BindInt64("frame_height", get_int64("frameHeight"));
      binder.BindDouble("frames_per_second", get_double("framesPerSecond"));
      binder.BindInt64("qp_sum", get_int64("qpSum"));
      binder.BindDouble("total_decode_time", get_double("totalDecodeTime"));
      binder.BindDouble("total_inter_frame_delay",
                        get_double("totalInterFrameDelay"));
      binder.BindDouble("total_squared_inter_frame_delay",
                        get_double("totalSquaredInterFrameDelay"));
      binder.BindInt64("pause_count", get_int64("pauseCount"));
      binder.BindDouble("total_pauses_duration",
                        get_double("totalPausesDuration"));
      binder.BindInt64("freeze_count", get_int64("freezeCount"));
      binder.BindDouble("total_freezes_duration",
                        get_double("totalFreezesDuration"));
      binder.BindDouble("total_processing_delay",
                        get_double("totalProcessingDelay"));
      binder.BindDouble("estimated_playout_timestamp",
                        get_double("estimatedPlayoutTimestamp"));
      binder.BindDouble("jitter_buffer_delay", get_double("jitterBufferDelay"));
      binder.BindDouble("jitter_buffer_target_delay",
                        get_double("jitterBufferTargetDelay"));
      binder.BindInt64("jitter_buffer_emitted_count",
                       get_int64("jitterBufferEmittedCount"));
      binder.BindDouble("jitter_buffer_minimum_delay",
                        get_double("jitterBufferMinimumDelay"));
      binder.BindInt64("total_samples_received",
                       get_int64("totalSamplesReceived"));
      binder.BindInt64("concealed_samples", get_int64("concealedSamples"));
      binder.BindInt64("silent_concealed_samples",
                       get_int64("silentConcealedSamples"));
      binder.BindInt64("concealment_events", get_int64("concealmentEvents"));
      binder.BindInt64("inserted_samples_for_deceleration",
                       get_int64("insertedSamplesForDeceleration"));
      binder.BindInt64("removed_samples_for_acceleration",
                       get_int64("removedSamplesForAcceleration"));
      binder.BindDouble("audio_level", get_double("audioLevel"));
      binder.BindDouble("total_audio_energy", get_double("totalAudioEnergy"));
      binder.BindDouble("total_samples_duration",
                        get_double("totalSamplesDuration"));
      binder.BindInt64("frames_received", get_int64("framesReceived"));
      binder.BindVarchar("decoder_implementation",
                         get_string("decoderImplementation").c_str());
      binder.BindVarchar("playout_id", get_string("playoutId").c_str());
      binder.BindBoolean("power_efficient_decoder",
                         get_bool("powerEfficientDecoder"));
      binder.BindInt64("frames_assembled_from_multiple_packets",
                       get_int64("framesAssembledFromMultiplePackets"));
      binder.BindDouble("total_assembly_time", get_double("totalAssemblyTime"));
      binder.BindInt64("retransmitted_packets_received",
                       get_int64("retransmittedPacketsReceived"));
      binder.BindInt64("retransmitted_bytes_received",
                       get_int64("retransmittedBytesReceived"));
      binder.BindInt64("rtx_ssrc", get_int64("rtxSsrc"));
      binder.BindInt64("fec_ssrc", get_int64("fecSsrc"));

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert inbound-rtp stats: "
                          << exec_result.error();
        return false;
      }

    } else if (rtc_type == "outbound-rtp") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_outbound_rtp (timestamp, channel_id, "
          "session_id, connection_id, rtc_timestamp, type, id, ssrc, kind, "
          "transport_id, codec_id, packets_sent, bytes_sent, mid, "
          "media_source_id, remote_id, rid, header_bytes_sent, "
          "retransmitted_packets_sent, retransmitted_bytes_sent, rtx_ssrc, "
          "target_bitrate, total_encoded_bytes_target, frame_width, "
          "frame_height, frames_per_second, frames_sent, huge_frames_sent, "
          "frames_encoded, key_frames_encoded, qp_sum, total_encode_time, "
          "total_packet_send_delay, quality_limitation_reason, "
          "quality_limitation_resolution_changes, nack_count, pli_count, "
          "fir_count, encoder_implementation, power_efficient_encoder, "
          "active, scalability_mode) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, $rtc_timestamp, $type, $id, $ssrc, $kind, "
          "$transport_id, $codec_id, $packets_sent, $bytes_sent, $mid, "
          "$media_source_id, $remote_id, $rid, $header_bytes_sent, "
          "$retransmitted_packets_sent, $retransmitted_bytes_sent, $rtx_ssrc, "
          "$target_bitrate, $total_encoded_bytes_target, $frame_width, "
          "$frame_height, $frames_per_second, $frames_sent, $huge_frames_sent, "
          "$frames_encoded, $key_frames_encoded, $qp_sum, $total_encode_time, "
          "$total_packet_send_delay, $quality_limitation_reason, "
          "$quality_limitation_resolution_changes, $nack_count, $pli_count, "
          "$fir_count, $encoder_implementation, $power_efficient_encoder, "
          "$active, $scalability_mode)";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare outbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindInt64("ssrc", get_int64("ssrc"));
      binder.BindVarchar("kind", get_string("kind").c_str());
      binder.BindVarchar("transport_id", get_string("transportId").c_str());
      binder.BindVarchar("codec_id", get_string("codecId").c_str());
      binder.BindInt64("packets_sent", get_int64("packetsSent"));
      binder.BindInt64("bytes_sent", get_int64("bytesSent"));
      binder.BindVarchar("mid", get_string("mid").c_str());
      binder.BindVarchar("media_source_id",
                         get_string("mediaSourceId").c_str());
      binder.BindVarchar("remote_id", get_string("remoteId").c_str());
      binder.BindVarchar("rid", get_string("rid").c_str());
      binder.BindInt64("header_bytes_sent", get_int64("headerBytesSent"));
      binder.BindInt64("retransmitted_packets_sent",
                       get_int64("retransmittedPacketsSent"));
      binder.BindInt64("retransmitted_bytes_sent",
                       get_int64("retransmittedBytesSent"));
      binder.BindInt64("rtx_ssrc", get_int64("rtxSsrc"));
      binder.BindDouble("target_bitrate", get_double("targetBitrate"));
      binder.BindInt64("total_encoded_bytes_target",
                       get_int64("totalEncodedBytesTarget"));
      binder.BindInt64("frame_width", get_int64("frameWidth"));
      binder.BindInt64("frame_height", get_int64("frameHeight"));
      binder.BindDouble("frames_per_second", get_double("framesPerSecond"));
      binder.BindInt64("frames_sent", get_int64("framesSent"));
      binder.BindInt64("huge_frames_sent", get_int64("hugeFramesSent"));
      binder.BindInt64("frames_encoded", get_int64("framesEncoded"));
      binder.BindInt64("key_frames_encoded", get_int64("keyFramesEncoded"));
      binder.BindInt64("qp_sum", get_int64("qpSum"));
      binder.BindDouble("total_encode_time", get_double("totalEncodeTime"));
      binder.BindDouble("total_packet_send_delay",
                        get_double("totalPacketSendDelay"));
      binder.BindVarchar("quality_limitation_reason",
                         get_string("qualityLimitationReason").c_str());
      binder.BindInt64("quality_limitation_resolution_changes",
                       get_int64("qualityLimitationResolutionChanges"));
      binder.BindInt64("nack_count", get_int64("nackCount"));
      binder.BindInt64("pli_count", get_int64("pliCount"));
      binder.BindInt64("fir_count", get_int64("firCount"));
      binder.BindVarchar("encoder_implementation",
                         get_string("encoderImplementation").c_str());
      binder.BindBoolean("power_efficient_encoder",
                         get_bool("powerEfficientEncoder"));
      binder.BindBoolean("active", get_bool("active"));
      binder.BindVarchar("scalability_mode",
                         get_string("scalabilityMode").c_str());

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert outbound-rtp stats: "
                          << exec_result.error();
        return false;
      }

    } else if (rtc_type == "media-source") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_media_source (timestamp, channel_id, "
          "session_id, connection_id, rtc_timestamp, type, id, "
          "track_identifier, kind, audio_level, total_audio_energy, "
          "total_samples_duration, echo_return_loss, "
          "echo_return_loss_enhancement, width, height, frames, "
          "frames_per_second) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, $rtc_timestamp, $type, $id, $track_identifier, "
          "$kind, $audio_level, $total_audio_energy, $total_samples_duration, "
          "$echo_return_loss, $echo_return_loss_enhancement, $width, $height, "
          "$frames, $frames_per_second)";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare media-source statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindVarchar("track_identifier",
                         get_string("trackIdentifier").c_str());
      binder.BindVarchar("kind", get_string("kind").c_str());
      binder.BindDouble("audio_level", get_double("audioLevel"));
      binder.BindDouble("total_audio_energy", get_double("totalAudioEnergy"));
      binder.BindDouble("total_samples_duration",
                        get_double("totalSamplesDuration"));
      binder.BindDouble("echo_return_loss", get_double("echoReturnLoss"));
      binder.BindDouble("echo_return_loss_enhancement",
                        get_double("echoReturnLossEnhancement"));
      binder.BindInt64("width", get_int64("width"));
      binder.BindInt64("height", get_int64("height"));
      binder.BindInt64("frames", get_int64("frames"));
      binder.BindDouble("frames_per_second", get_double("framesPerSecond"));

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert media-source stats: "
                          << exec_result.error();
        return false;
      }

    } else if (rtc_type == "remote-inbound-rtp") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_remote_inbound_rtp (timestamp, channel_id, "
          "session_id, connection_id, rtc_timestamp, type, id, ssrc, kind, "
          "transport_id, codec_id, packets_received, packets_lost, jitter, "
          "local_id, round_trip_time, total_round_trip_time, fraction_lost, "
          "round_trip_time_measurements) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, $rtc_timestamp, $type, $id, $ssrc, $kind, "
          "$transport_id, $codec_id, $packets_received, $packets_lost, "
          "$jitter, $local_id, $round_trip_time, $total_round_trip_time, "
          "$fraction_lost, $round_trip_time_measurements)";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare remote-inbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindInt64("ssrc", get_int64("ssrc"));
      binder.BindVarchar("kind", get_string("kind").c_str());
      binder.BindVarchar("transport_id", get_string("transportId").c_str());
      binder.BindVarchar("codec_id", get_string("codecId").c_str());
      binder.BindInt64("packets_received", get_int64("packetsReceived"));
      binder.BindInt64("packets_lost", get_int64("packetsLost"));
      binder.BindDouble("jitter", get_double("jitter"));
      binder.BindVarchar("local_id", get_string("localId").c_str());
      binder.BindDouble("round_trip_time", get_double("roundTripTime"));
      binder.BindDouble("total_round_trip_time",
                        get_double("totalRoundTripTime"));
      binder.BindDouble("fraction_lost", get_double("fractionLost"));
      binder.BindInt64("round_trip_time_measurements",
                       get_int64("roundTripTimeMeasurements"));

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert remote-inbound-rtp stats: "
                          << exec_result.error();
        return false;
      }

    } else if (rtc_type == "remote-outbound-rtp") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_remote_outbound_rtp (timestamp, channel_id, "
          "session_id, connection_id, rtc_timestamp, type, id, ssrc, kind, "
          "transport_id, codec_id, packets_sent, bytes_sent, local_id, "
          "remote_timestamp, reports_sent, round_trip_time, "
          "total_round_trip_time, round_trip_time_measurements) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, $rtc_timestamp, $type, $id, $ssrc, $kind, "
          "$transport_id, $codec_id, $packets_sent, $bytes_sent, $local_id, "
          "$remote_timestamp, $reports_sent, $round_trip_time, "
          "$total_round_trip_time, $round_trip_time_measurements)";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare remote-outbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindInt64("ssrc", get_int64("ssrc"));
      binder.BindVarchar("kind", get_string("kind").c_str());
      binder.BindVarchar("transport_id", get_string("transportId").c_str());
      binder.BindVarchar("codec_id", get_string("codecId").c_str());
      binder.BindInt64("packets_sent", get_int64("packetsSent"));
      binder.BindInt64("bytes_sent", get_int64("bytesSent"));
      binder.BindVarchar("local_id", get_string("localId").c_str());
      binder.BindDouble("remote_timestamp", get_double("remoteTimestamp"));
      binder.BindInt64("reports_sent", get_int64("reportsSent"));
      binder.BindDouble("round_trip_time", get_double("roundTripTime"));
      binder.BindDouble("total_round_trip_time",
                        get_double("totalRoundTripTime"));
      binder.BindInt64("round_trip_time_measurements",
                       get_int64("roundTripTimeMeasurements"));

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert remote-outbound-rtp stats: "
                          << exec_result.error();
        return false;
      }

    } else if (rtc_type == "data-channel") {
      duckdb_utils::PreparedStatement stmt;
      const char* sql =
          "INSERT INTO rtc_stats_data_channel (timestamp, channel_id, "
          "session_id, connection_id, rtc_timestamp, type, id, label, "
          "protocol, data_channel_identifier, state, messages_sent, "
          "bytes_sent, messages_received, bytes_received) "
          "VALUES (CURRENT_TIMESTAMP, $channel_id, $session_id, "
          "$connection_id, $rtc_timestamp, $type, $id, $label, $protocol, "
          "$data_channel_identifier, $state, $messages_sent, $bytes_sent, "
          "$messages_received, $bytes_received)";
      if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare data-channel statement: "
                          << stmt.error();
        return false;
      }

      duckdb_utils::NamedBinder binder(stmt.get_raw());
      binder.BindVarchar("channel_id", channel_id.c_str());
      binder.BindVarchar("session_id", session_id.c_str());
      binder.BindVarchar("connection_id", connection_id.c_str());
      binder.BindDouble("rtc_timestamp", rtc_timestamp);
      binder.BindVarchar("type", get_string("type").c_str());
      binder.BindVarchar("id", get_string("id").c_str());
      binder.BindVarchar("label", get_string("label").c_str());
      binder.BindVarchar("protocol", get_string("protocol").c_str());
      binder.BindInt64("data_channel_identifier",
                       get_int64("dataChannelIdentifier"));
      binder.BindVarchar("state", get_string("state").c_str());
      binder.BindInt64("messages_sent", get_int64("messagesSent"));
      binder.BindInt64("bytes_sent", get_int64("bytesSent"));
      binder.BindInt64("messages_received", get_int64("messagesReceived"));
      binder.BindInt64("bytes_received", get_int64("bytesReceived"));

      duckdb_utils::Result exec_result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
        RTC_LOG(LS_ERROR) << "Failed to insert data-channel stats: "
                          << exec_result.error();
        return false;
      }
    }

    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error in WriteRTCStats: " << e.what();
    return false;
  }
}

bool DuckDBStatsWriter::WriteZakuroInfo(const std::string& config_mode,
                                        const std::string& config_json) {
  if (!initialized_) {
    RTC_LOG(LS_WARNING)
        << "DuckDBStatsWriter::WriteZakuroInfo - not initialized";
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  try {
    auto deps = ReadDepsFile();

    duckdb_utils::PreparedStatement stmt;
    const char* sql =
        "INSERT INTO zakuro (version, environment, webrtc_version, "
        "sora_cpp_sdk_version, boost_version, cli11_version, cmake_version, "
        "blend2d_version, openh264_version, duckdb_version, config_mode, "
        "config_json, start_timestamp) "
        "VALUES ($version, $environment, $webrtc_version, "
        "$sora_cpp_sdk_version, $boost_version, $cli11_version, "
        "$cmake_version, $blend2d_version, $openh264_version, "
        "$duckdb_version, $config_mode, $config_json, CURRENT_TIMESTAMP)";
    if (!duckdb_utils::Prepare(conn_, sql, stmt)) {
      RTC_LOG(LS_ERROR) << "Failed to prepare zakuro info statement: "
                        << stmt.error();
      return false;
    }

    duckdb_utils::NamedBinder binder(stmt.get_raw());
    binder.BindVarchar("version", ZakuroVersion::GetVersion().c_str());
    binder.BindVarchar("environment",
                       ZakuroVersion::GetEnvironmentName().c_str());
    binder.BindVarchar("webrtc_version",
                       ZakuroVersion::GetWebRTCVersion().c_str());
    binder.BindVarchar("sora_cpp_sdk_version",
                       ZakuroVersion::GetSoraCppSdkVersion().c_str());
    binder.BindVarchar("boost_version", deps["BOOST_VERSION"].c_str());
    binder.BindVarchar("cli11_version", deps["CLI11_VERSION"].c_str());
    binder.BindVarchar("cmake_version", deps["CMAKE_VERSION"].c_str());
    binder.BindVarchar("blend2d_version", deps["BLEND2D_VERSION"].c_str());
    binder.BindVarchar("openh264_version", deps["OPENH264_VERSION"].c_str());
    binder.BindVarchar("duckdb_version", deps["DUCKDB_VERSION"].c_str());
    binder.BindVarchar("config_mode", config_mode.c_str());
    binder.BindVarchar("config_json", config_json.c_str());

    duckdb_utils::Result exec_result;
    if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), exec_result)) {
      RTC_LOG(LS_ERROR) << "Failed to insert zakuro info: "
                        << exec_result.error();
      return false;
    }

    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Error in WriteZakuroInfo: " << e.what();
    return false;
  }
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

  // 結果を JSON 形式に変換
  boost::json::object response;
  boost::json::array rows;

  // カラム情報を取得
  idx_t column_count = result.column_count();
  idx_t row_count = result.row_count();

  rows.reserve(row_count);

  for (idx_t row = 0; row < row_count; row++) {
    boost::json::object row_obj;

    for (idx_t col = 0; col < column_count; col++) {
      const char* col_name = duckdb_column_name(result.get(), col);

      // NULL 値のチェック
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
        case DUCKDB_TYPE_VARCHAR:
        case DUCKDB_TYPE_TIMESTAMP:
        default: {
          // 文字列として取得
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
  return boost::json::serialize(response);
}
