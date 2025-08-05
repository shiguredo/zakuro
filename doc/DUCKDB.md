# Zakuro DuckDB スキーマドキュメント

Zakuro は WebRTC の統計情報を DuckDB データベースに保存します。データベースファイルは `zakuro_YYYYMMDD_HHMMSS_mmm.db` 形式で生成されます。

## テーブル一覧

- `zakuro` - Zakuro 起動情報
- `zakuro_scenario` - シナリオ設定情報
- `connection` - 接続情報
- `rtc_stats_codec` - コーデック統計情報
- `rtc_stats_inbound_rtp` - 受信 RTP ストリーム統計情報
- `rtc_stats_outbound_rtp` - 送信 RTP ストリーム統計情報
- `rtc_stats_media_source` - メディアソース統計情報
- `rtc_stats_remote_inbound_rtp` - リモート受信 RTP ストリーム統計情報
- `rtc_stats_remote_outbound_rtp` - リモート送信 RTP ストリーム統計情報
- `rtc_stats_data_channel` - データチャネル統計情報

## テーブルスキーマ

### zakuro テーブル

Zakuro 起動時の環境情報と設定、およびプロセスの実行時間を記録します。

```sql
CREATE TABLE zakuro (
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
    config_mode VARCHAR,  -- 'ARGS' or 'YAML'
    config_json JSON,  -- 引数または YAML の設定を JSON として保存
    start_timestamp TIMESTAMP,  -- プロセス開始時刻
    stop_timestamp TIMESTAMP  -- プロセス終了時刻
)
```

### zakuro_scenario テーブル

シナリオ実行時の設定情報を記録します。

```sql
CREATE TABLE zakuro_scenario (
    vcs INTEGER,
    duration DOUBLE,
    repeat_interval DOUBLE,
    max_retry INTEGER,
    retry_interval DOUBLE,
    sora_signaling_urls VARCHAR[],
    sora_channel_id VARCHAR,
    sora_role VARCHAR
)
```

### connection テーブル

接続情報を記録します。

```sql
CREATE TABLE connection (
    pk BIGINT PRIMARY KEY DEFAULT nextval('connection_pk_seq'),
    timestamp TIMESTAMP,
    channel_id VARCHAR,
    connection_id VARCHAR,
    session_id VARCHAR,
    audio BOOLEAN,
    video BOOLEAN,
    websocket_connected BOOLEAN,
    datachannel_connected BOOLEAN
)
```

### rtc_stats_codec テーブル

コーデック情報を記録します。

```sql
CREATE TABLE rtc_stats_codec (
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
```

### rtc_stats_inbound_rtp テーブル

受信 RTP ストリームの統計情報を記録します。

```sql
CREATE TABLE rtc_stats_inbound_rtp (
    pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_inbound_rtp_pk_seq'),
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
```

### rtc_stats_outbound_rtp テーブル

送信 RTP ストリームの統計情報を記録します。

```sql
CREATE TABLE rtc_stats_outbound_rtp (
    pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_outbound_rtp_pk_seq'),
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
    -- TODO: psnrSum と psnrMeasurements は record<DOMString, double> 型なので実装が必要
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
```

### rtc_stats_media_source テーブル

メディアソースの統計情報を記録します。

```sql
CREATE TABLE rtc_stats_media_source (
    pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_media_source_pk_seq'),
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
```

### rtc_stats_remote_inbound_rtp テーブル

リモート受信 RTP ストリームの統計情報を記録します。

```sql
CREATE TABLE rtc_stats_remote_inbound_rtp (
    pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_remote_inbound_rtp_pk_seq'),
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
    packets_received_with_ect1 BIGINT,
    packets_received_with_ce BIGINT,
    packets_reported_as_lost BIGINT,
    packets_reported_as_lost_but_recovered BIGINT,
    packets_lost BIGINT,
    jitter DOUBLE,
    -- RTCRemoteInboundRtpStreamStats
    local_id VARCHAR,
    round_trip_time DOUBLE,
    total_round_trip_time DOUBLE,
    fraction_lost DOUBLE,
    round_trip_time_measurements BIGINT,
    packets_with_bleached_ect1_marking BIGINT
)
```

### rtc_stats_remote_outbound_rtp テーブル

リモート送信 RTP ストリームの統計情報を記録します。

```sql
CREATE TABLE rtc_stats_remote_outbound_rtp (
    pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_remote_outbound_rtp_pk_seq'),
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
    -- RTCRemoteOutboundRtpStreamStats
    local_id VARCHAR,
    remote_timestamp DOUBLE,
    reports_sent BIGINT,
    round_trip_time DOUBLE,
    total_round_trip_time DOUBLE,
    round_trip_time_measurements BIGINT
)
```

### rtc_stats_data_channel テーブル

データチャネルの統計情報を記録します。

```sql
CREATE TABLE rtc_stats_data_channel (
    pk BIGINT PRIMARY KEY DEFAULT nextval('rtc_stats_data_channel_pk_seq'),
    timestamp TIMESTAMP,
    channel_id VARCHAR,
    session_id VARCHAR,
    connection_id VARCHAR,
    rtc_timestamp DOUBLE,
    -- RTCStats
    type VARCHAR,
    id VARCHAR,
    -- RTCDataChannelStats
    label VARCHAR,
    protocol VARCHAR,
    data_channel_identifier SMALLINT,
    state VARCHAR,  -- REQUIRED field
    messages_sent BIGINT,
    bytes_sent BIGINT,
    messages_received BIGINT,
    bytes_received BIGINT
)
```

## シーケンス

各テーブルの主キー用シーケンス：

- `connection_pk_seq`
- `rtc_stats_codec_pk_seq`
- `rtc_stats_inbound_rtp_pk_seq`
- `rtc_stats_outbound_rtp_pk_seq`
- `rtc_stats_media_source_pk_seq`
- `rtc_stats_remote_inbound_rtp_pk_seq`
- `rtc_stats_remote_outbound_rtp_pk_seq`
- `rtc_stats_data_channel_pk_seq`

## インデックス

パフォーマンス最適化のために以下のインデックスが作成されています：

```sql
-- connection テーブル
CREATE INDEX idx_connection_id ON connection(connection_id);
CREATE INDEX idx_connection_composite ON connection(channel_id, timestamp);

-- 各統計情報テーブル
CREATE INDEX idx_rtc_stats_codec_composite ON rtc_stats_codec(channel_id, connection_id, timestamp);
CREATE INDEX idx_rtc_stats_inbound_rtp_composite ON rtc_stats_inbound_rtp(channel_id, connection_id, timestamp);
CREATE INDEX idx_rtc_stats_outbound_rtp_composite ON rtc_stats_outbound_rtp(channel_id, connection_id, timestamp);
CREATE INDEX idx_rtc_stats_media_source_composite ON rtc_stats_media_source(channel_id, connection_id, timestamp);
CREATE INDEX idx_rtc_stats_remote_inbound_rtp_composite ON rtc_stats_remote_inbound_rtp(channel_id, connection_id, timestamp);
CREATE INDEX idx_rtc_stats_remote_outbound_rtp_composite ON rtc_stats_remote_outbound_rtp(channel_id, connection_id, timestamp);
CREATE INDEX idx_rtc_stats_data_channel_composite ON rtc_stats_data_channel(channel_id, connection_id, timestamp);
```

## 注意事項

- `rtc_stats_codec` テーブルには UNIQUE 制約があるため、同じコーデック情報は重複して挿入されません
- すべてのタイムスタンプは UTC で記録されます
- `rtc_timestamp` は WebRTC の performance.timeOrigin + performance.now() の値（ミリ秒）です
- `rtc_stats_outbound_rtp` の `psnrSum` と `psnrMeasurements` フィールドは現在未実装です
- `zakuro` テーブルの `start_timestamp` はプロセス起動時に自動的に記録されます
- `zakuro` テーブルの `stop_timestamp` はプロセス終了時（正常終了またはシグナル受信時）に記録されます
