#include "duckdb_stats_writer.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include <boost/json.hpp>
#include <rtc_base/logging.h>

#include "virtual_client.h"
#include "zakuro_version.h"

namespace {

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

// JSON から文字列を安全に取得
std::string get_string(const boost::json::object& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end() || !it->value().is_string()) {
    return "";
  }
  return std::string(it->value().as_string());
}

// JSON から int64 を安全に取得
int64_t get_int64(const boost::json::object& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    return 0;
  }
  if (it->value().is_int64()) {
    return it->value().as_int64();
  }
  if (it->value().is_uint64()) {
    return static_cast<int64_t>(it->value().as_uint64());
  }
  return 0;
}

// JSON から double を安全に取得
double get_double(const boost::json::object& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    return 0.0;
  }
  if (it->value().is_double()) {
    return it->value().as_double();
  }
  if (it->value().is_int64()) {
    return static_cast<double>(it->value().as_int64());
  }
  if (it->value().is_uint64()) {
    return static_cast<double>(it->value().as_uint64());
  }
  return 0.0;
}

// JSON から bool を安全に取得
bool get_bool(const boost::json::object& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end() || !it->value().is_bool()) {
    return false;
  }
  return it->value().as_bool();
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
  RTC_LOG(LS_VERBOSE) << "DuckDB file: " << filename;

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
    CreateTable();
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to create tables: " << e.what();
    CleanupResources();
    return false;
  }

  initialized_ = true;
  return true;
}

void DuckDBStatsWriter::CreateTable() {
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

  // zakuro テーブルを作成（zakuro_scenario を統合）
  std::string create_zakuro_table_sql = R"(
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
      yaml_cpp_version VARCHAR,
      duckdb_version VARCHAR,
      config_file_path TEXT,
      global_config_json TEXT,
      instances_json TEXT,
      start_timestamp TIMESTAMP,
      stop_timestamp TIMESTAMP
    )
  )";
  execute_query(create_zakuro_table_sql);

  // connection テーブルを作成
  std::string create_connection_table_sql = R"(
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
  )";
  execute_query(create_connection_table_sql);

  // rtc_stats_codec テーブルを作成
  std::string create_codec_table_sql = R"(
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
  )";
  execute_query(create_codec_table_sql);

  // rtc_stats_inbound_rtp テーブルを作成
  std::string create_inbound_table_sql = R"(
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
  )";
  execute_query(create_inbound_table_sql);

  // rtc_stats_outbound_rtp テーブルを作成
  std::string create_outbound_table_sql = R"(
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

  // rtc_stats_media_source テーブルを作成
  std::string create_media_source_table_sql = R"(
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
  )";
  execute_query(create_media_source_table_sql);

  // rtc_stats_remote_inbound_rtp テーブルを作成
  std::string create_remote_inbound_table_sql = R"(
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
  )";
  execute_query(create_remote_inbound_table_sql);

  // rtc_stats_remote_outbound_rtp テーブルを作成
  std::string create_remote_outbound_table_sql = R"(
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
  )";
  execute_query(create_remote_outbound_table_sql);

  // rtc_stats_data_channel テーブルを作成
  std::string create_data_channel_table_sql = R"(
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
  )";
  execute_query(create_data_channel_table_sql);

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
#ifdef _WIN32
  localtime_s(&tm_now, &time_t_now);
#else
  localtime_r(&time_t_now, &tm_now);
#endif

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

bool DuckDBStatsWriter::WriteStats(const std::vector<VirtualClientStats>& stats) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    RTC_LOG(LS_ERROR) << "DuckDB not initialized";
    return false;
  }

  try {
    duckdb_utils::Transaction transaction(conn_);

    for (const auto& stat : stats) {
      if (stat.connection_id.empty()) {
        continue;
      }

      std::string insert_sql = R"(
        INSERT INTO connection (
          timestamp, channel_id, connection_id, session_id,
          role, audio, video, websocket_connected, datachannel_connected
        ) VALUES (CURRENT_TIMESTAMP, ?, ?, ?, ?, ?, ?, ?, ?)
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare statement: " << stmt.error();
        return false;
      }

      duckdb_bind_varchar(stmt.get_raw(), 1, stat.channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), 2, stat.connection_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), 3, stat.session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), 4, stat.role.c_str());
      duckdb_bind_boolean(stmt.get_raw(), 5, stat.has_audio_track);
      duckdb_bind_boolean(stmt.get_raw(), 6, stat.has_video_track);
      duckdb_bind_boolean(stmt.get_raw(), 7, stat.websocket_connected);
      duckdb_bind_boolean(stmt.get_raw(), 8, stat.datachannel_connected);

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute statement: " << result.error();
        return false;
      }
    }

    transaction.Commit();
    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to write stats: " << e.what();
    return false;
  }
}

bool DuckDBStatsWriter::WriteZakuroInfo(const std::string& config_file_path,
                                        const std::string& global_config_json,
                                        const std::string& instances_json) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    RTC_LOG(LS_ERROR) << "DuckDB not initialized";
    return false;
  }

  try {
    auto deps = ReadDepsFile();

    std::string insert_sql = R"(
      INSERT INTO zakuro (
        version, environment, webrtc_version, sora_cpp_sdk_version,
        boost_version, cli11_version, cmake_version, blend2d_version,
        openh264_version, yaml_cpp_version, duckdb_version,
        config_file_path, global_config_json, instances_json,
        start_timestamp
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
    )";

    duckdb_utils::PreparedStatement stmt;
    if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
      RTC_LOG(LS_ERROR) << "Failed to prepare statement: " << stmt.error();
      return false;
    }

    // インデックスベースのバインド（1から開始）
    duckdb_bind_varchar(stmt.get_raw(), 1, ZakuroVersion::GetVersion().c_str());
    duckdb_bind_varchar(stmt.get_raw(), 2, ZakuroVersion::GetEnvironmentName().c_str());
    duckdb_bind_varchar(stmt.get_raw(), 3, ZakuroVersion::GetWebRTCVersion().c_str());
    duckdb_bind_varchar(stmt.get_raw(), 4, ZakuroVersion::GetSoraCppSdkVersion().c_str());
    duckdb_bind_varchar(stmt.get_raw(), 5, deps["BOOST_VERSION"].c_str());
    duckdb_bind_varchar(stmt.get_raw(), 6, deps["CLI11_VERSION"].c_str());
    duckdb_bind_varchar(stmt.get_raw(), 7, deps["CMAKE_VERSION"].c_str());
    duckdb_bind_varchar(stmt.get_raw(), 8, deps["BLEND2D_VERSION"].c_str());
    duckdb_bind_varchar(stmt.get_raw(), 9, deps["OPENH264_VERSION"].c_str());
    duckdb_bind_varchar(stmt.get_raw(), 10, "");  // yaml_cpp_version
    duckdb_bind_varchar(stmt.get_raw(), 11, deps["DUCKDB_VERSION"].c_str());
    duckdb_bind_varchar(stmt.get_raw(), 12, config_file_path.c_str());
    duckdb_bind_varchar(stmt.get_raw(), 13, global_config_json.c_str());
    duckdb_bind_varchar(stmt.get_raw(), 14, instances_json.c_str());

    duckdb_utils::Result result;
    if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
      RTC_LOG(LS_ERROR) << "Failed to execute statement: " << result.error();
      return false;
    }

    RTC_LOG(LS_INFO) << "Successfully wrote zakuro info to DuckDB";
    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to write zakuro info: " << e.what();
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
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    RTC_LOG(LS_ERROR) << "DuckDB not initialized";
    return false;
  }

  try {
    boost::system::error_code ec;
    boost::json::value jv = boost::json::parse(rtc_data_json, ec);
    if (ec) {
      RTC_LOG(LS_ERROR) << "Failed to parse JSON: " << ec.message();
      return false;
    }

    if (!jv.is_object()) {
      RTC_LOG(LS_ERROR) << "JSON is not an object";
      return false;
    }

    const auto& obj = jv.as_object();

    if (rtc_type == "codec") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_codec (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, mime_type, payload_type, clock_rate, channels, sdp_fmtp_line
        ) VALUES (to_timestamp(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT DO NOTHING
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare codec statement: "
                          << stmt.error();
        return false;
      }

      duckdb_bind_double(stmt.get_raw(), 1, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), 2, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), 3, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), 4, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), 5, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), 6, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), 7, get_string(obj, "id").c_str());
      duckdb_bind_varchar(stmt.get_raw(), 8, get_string(obj, "mimeType").c_str());
      duckdb_bind_int64(stmt.get_raw(), 9, get_int64(obj, "payloadType"));
      duckdb_bind_int64(stmt.get_raw(), 10, get_int64(obj, "clockRate"));
      duckdb_bind_int64(stmt.get_raw(), 11, get_int64(obj, "channels"));
      duckdb_bind_varchar(stmt.get_raw(), 12, get_string(obj, "sdpFmtpLine").c_str());

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute codec statement: "
                          << result.error();
        return false;
      }
    } else if (rtc_type == "inbound-rtp") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_inbound_rtp (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, ssrc, kind, transport_id, codec_id,
          packets_received, packets_lost, bytes_received, jitter,
          last_packet_received_timestamp, header_bytes_received, packets_discarded,
          fec_bytes_received, fec_packets_received, fec_packets_discarded,
          nack_count, pli_count, fir_count, track_identifier, mid, remote_id,
          frames_decoded, key_frames_decoded, frames_rendered, frames_dropped,
          frame_width, frame_height, frames_per_second, qp_sum,
          total_decode_time, total_inter_frame_delay, total_squared_inter_frame_delay,
          pause_count, total_pauses_duration, freeze_count, total_freezes_duration,
          total_processing_delay, estimated_playout_timestamp,
          jitter_buffer_delay, jitter_buffer_target_delay, jitter_buffer_emitted_count,
          jitter_buffer_minimum_delay, total_samples_received, concealed_samples,
          silent_concealed_samples, concealment_events, inserted_samples_for_deceleration,
          removed_samples_for_acceleration, audio_level, total_audio_energy,
          total_samples_duration, frames_received, decoder_implementation, playout_id,
          power_efficient_decoder, frames_assembled_from_multiple_packets,
          total_assembly_time, retransmitted_packets_received, retransmitted_bytes_received,
          rtx_ssrc, fec_ssrc
        ) VALUES (
          to_timestamp(?), ?, ?, ?, ?,
          ?, ?, ?, ?, ?, ?,
          ?, ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?, ?, ?, ?,
          ?, ?, ?, ?,
          ?, ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?, ?,
          ?, ?,
          ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?, ?,
          ?, ?,
          ?, ?, ?,
          ?, ?
        )
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare inbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      int idx = 1;
      duckdb_bind_double(stmt.get_raw(), idx++, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "id").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "ssrc"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "kind").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "transportId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "codecId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsLost"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "bytesReceived"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "jitter"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "lastPacketReceivedTimestamp"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "headerBytesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsDiscarded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "fecBytesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "fecPacketsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "fecPacketsDiscarded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "nackCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "pliCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "firCount"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "trackIdentifier").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "mid").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "remoteId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesDecoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "keyFramesDecoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesRendered"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesDropped"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "frameWidth"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "frameHeight"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "framesPerSecond"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "qpSum"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalDecodeTime"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalInterFrameDelay"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalSquaredInterFrameDelay"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "pauseCount"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalPausesDuration"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "freezeCount"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalFreezesDuration"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalProcessingDelay"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "estimatedPlayoutTimestamp"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "jitterBufferDelay"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "jitterBufferTargetDelay"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "jitterBufferEmittedCount"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "jitterBufferMinimumDelay"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "totalSamplesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "concealedSamples"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "silentConcealedSamples"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "concealmentEvents"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "insertedSamplesForDeceleration"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "removedSamplesForAcceleration"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "audioLevel"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalAudioEnergy"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalSamplesDuration"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesReceived"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "decoderImplementation").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "playoutId").c_str());
      duckdb_bind_boolean(stmt.get_raw(), idx++, get_bool(obj, "powerEfficientDecoder"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesAssembledFromMultiplePackets"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalAssemblyTime"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "retransmittedPacketsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "retransmittedBytesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "rtxSsrc"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "fecSsrc"));

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute inbound-rtp statement: "
                          << result.error();
        return false;
      }
    } else if (rtc_type == "outbound-rtp") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_outbound_rtp (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, ssrc, kind, transport_id, codec_id,
          packets_sent, bytes_sent, mid, media_source_id, remote_id, rid,
          header_bytes_sent, retransmitted_packets_sent, retransmitted_bytes_sent,
          rtx_ssrc, target_bitrate, total_encoded_bytes_target,
          frame_width, frame_height, frames_per_second, frames_sent, huge_frames_sent,
          frames_encoded, key_frames_encoded, qp_sum, total_encode_time,
          total_packet_send_delay, quality_limitation_reason,
          quality_limitation_duration_none, quality_limitation_duration_cpu,
          quality_limitation_duration_bandwidth, quality_limitation_duration_other,
          quality_limitation_resolution_changes, nack_count, pli_count, fir_count,
          encoder_implementation, power_efficient_encoder, active, scalability_mode
        ) VALUES (
          to_timestamp(?), ?, ?, ?, ?,
          ?, ?, ?, ?, ?, ?,
          ?, ?, ?, ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?,
          ?, ?, ?, ?, ?,
          ?, ?, ?, ?,
          ?, ?,
          ?, ?,
          ?, ?,
          ?, ?, ?, ?,
          ?, ?, ?, ?
        )
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare outbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      // quality_limitation_durations の処理
      double qld_none = 0.0, qld_cpu = 0.0, qld_bandwidth = 0.0, qld_other = 0.0;
      auto qld_it = obj.find("qualityLimitationDurations");
      if (qld_it != obj.end() && qld_it->value().is_object()) {
        const auto& qld = qld_it->value().as_object();
        qld_none = get_double(qld, "none");
        qld_cpu = get_double(qld, "cpu");
        qld_bandwidth = get_double(qld, "bandwidth");
        qld_other = get_double(qld, "other");
      }

      int idx = 1;
      duckdb_bind_double(stmt.get_raw(), idx++, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "id").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "ssrc"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "kind").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "transportId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "codecId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "bytesSent"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "mid").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "mediaSourceId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "remoteId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "rid").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "headerBytesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "retransmittedPacketsSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "retransmittedBytesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "rtxSsrc"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "targetBitrate"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "totalEncodedBytesTarget"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "frameWidth"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "frameHeight"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "framesPerSecond"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "hugeFramesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "framesEncoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "keyFramesEncoded"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "qpSum"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalEncodeTime"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalPacketSendDelay"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "qualityLimitationReason").c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, qld_none);
      duckdb_bind_double(stmt.get_raw(), idx++, qld_cpu);
      duckdb_bind_double(stmt.get_raw(), idx++, qld_bandwidth);
      duckdb_bind_double(stmt.get_raw(), idx++, qld_other);
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "qualityLimitationResolutionChanges"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "nackCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "pliCount"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "firCount"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "encoderImplementation").c_str());
      duckdb_bind_boolean(stmt.get_raw(), idx++, get_bool(obj, "powerEfficientEncoder"));
      duckdb_bind_boolean(stmt.get_raw(), idx++, get_bool(obj, "active"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "scalabilityMode").c_str());

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute outbound-rtp statement: "
                          << result.error();
        return false;
      }
    } else if (rtc_type == "media-source") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_media_source (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, track_identifier, kind,
          audio_level, total_audio_energy, total_samples_duration,
          echo_return_loss, echo_return_loss_enhancement,
          width, height, frames, frames_per_second
        ) VALUES (to_timestamp(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare media-source statement: "
                          << stmt.error();
        return false;
      }

      int idx = 1;
      duckdb_bind_double(stmt.get_raw(), idx++, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "id").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "trackIdentifier").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "kind").c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "audioLevel"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalAudioEnergy"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalSamplesDuration"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "echoReturnLoss"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "echoReturnLossEnhancement"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "width"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "height"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "frames"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "framesPerSecond"));

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute media-source statement: "
                          << result.error();
        return false;
      }
    } else if (rtc_type == "remote-inbound-rtp") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_remote_inbound_rtp (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, ssrc, kind, transport_id, codec_id,
          packets_received, packets_lost, jitter,
          local_id, round_trip_time, total_round_trip_time,
          fraction_lost, round_trip_time_measurements
        ) VALUES (to_timestamp(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare remote-inbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      int idx = 1;
      duckdb_bind_double(stmt.get_raw(), idx++, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "id").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "ssrc"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "kind").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "transportId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "codecId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsLost"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "jitter"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "localId").c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "roundTripTime"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalRoundTripTime"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "fractionLost"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "roundTripTimeMeasurements"));

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute remote-inbound-rtp statement: "
                          << result.error();
        return false;
      }
    } else if (rtc_type == "remote-outbound-rtp") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_remote_outbound_rtp (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, ssrc, kind, transport_id, codec_id,
          packets_sent, bytes_sent,
          local_id, remote_timestamp, reports_sent,
          round_trip_time, total_round_trip_time, round_trip_time_measurements
        ) VALUES (to_timestamp(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare remote-outbound-rtp statement: "
                          << stmt.error();
        return false;
      }

      int idx = 1;
      duckdb_bind_double(stmt.get_raw(), idx++, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "id").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "ssrc"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "kind").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "transportId").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "codecId").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "packetsSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "bytesSent"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "localId").c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "remoteTimestamp"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "reportsSent"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "roundTripTime"));
      duckdb_bind_double(stmt.get_raw(), idx++, get_double(obj, "totalRoundTripTime"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "roundTripTimeMeasurements"));

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute remote-outbound-rtp statement: "
                          << result.error();
        return false;
      }
    } else if (rtc_type == "data-channel") {
      std::string insert_sql = R"(
        INSERT INTO rtc_stats_data_channel (
          timestamp, channel_id, session_id, connection_id, rtc_timestamp,
          type, id, label, protocol, data_channel_identifier, state,
          messages_sent, bytes_sent, messages_received, bytes_received
        ) VALUES (to_timestamp(?), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      )";

      duckdb_utils::PreparedStatement stmt;
      if (!duckdb_utils::Prepare(conn_, insert_sql, stmt)) {
        RTC_LOG(LS_ERROR) << "Failed to prepare data-channel statement: "
                          << stmt.error();
        return false;
      }

      int idx = 1;
      duckdb_bind_double(stmt.get_raw(), idx++, timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, channel_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, session_id.c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, connection_id.c_str());
      duckdb_bind_double(stmt.get_raw(), idx++, rtc_timestamp);
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "type").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "id").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "label").c_str());
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "protocol").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "dataChannelIdentifier"));
      duckdb_bind_varchar(stmt.get_raw(), idx++, get_string(obj, "state").c_str());
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "messagesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "bytesSent"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "messagesReceived"));
      duckdb_bind_int64(stmt.get_raw(), idx++, get_int64(obj, "bytesReceived"));

      duckdb_utils::Result result;
      if (!duckdb_utils::ExecutePrepared(stmt.get_raw(), result)) {
        RTC_LOG(LS_ERROR) << "Failed to execute data-channel statement: "
                          << result.error();
        return false;
      }
    }

    return true;
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Failed to write RTC stats: " << e.what();
    return false;
  }
}

void DuckDBStatsWriter::Close() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return;
  }

  // stop_timestamp を記録
  duckdb_utils::Result result;
  if (!duckdb_utils::ExecuteQuery(
          conn_,
          "UPDATE zakuro SET stop_timestamp = CURRENT_TIMESTAMP "
          "WHERE start_timestamp = (SELECT MAX(start_timestamp) FROM zakuro)",
          result)) {
    RTC_LOG(LS_ERROR) << "Failed to update stop_timestamp: " << result.error();
  }

  // CHECKPOINT を実行してデータベースを同期
  if (!duckdb_utils::ExecuteQuery(conn_, "CHECKPOINT", result)) {
    RTC_LOG(LS_ERROR) << "Failed to checkpoint: " << result.error();
  }

  CleanupResources();
}
