# Zakuro DuckDB スキーマドキュメント

Zakuro は WebRTC の統計情報を DuckDB データベースに保存します。データベースファイルは `zakuro_stats_YYYYMMDD_HHMMSS_mmm.ddb` 形式で生成されます。

## テーブル一覧

- `connections` - 接続情報
- `codec_stats` - コーデック統計情報
- `inbound_rtp_stats` - 受信 RTP ストリーム統計情報
- `outbound_rtp_stats` - 送信 RTP ストリーム統計情報
- `media_source_stats` - メディアソース統計情報

## テーブルスキーマ

### connections テーブル

接続情報を記録します。

```sql
CREATE TABLE connections (
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
```

### codec_stats テーブル

コーデック情報を記録します。

```sql
CREATE TABLE codec_stats (
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
```

### inbound_rtp_stats テーブル

受信 RTP ストリームの統計情報を記録します。

```sql
CREATE TABLE inbound_rtp_stats (
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
```

### outbound_rtp_stats テーブル

送信 RTP ストリームの統計情報を記録します。

```sql
CREATE TABLE outbound_rtp_stats (
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

### media_source_stats テーブル

メディアソースの統計情報を記録します。

```sql
CREATE TABLE media_source_stats (
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
```

## シーケンス

各テーブルの主キー用シーケンス：

- `connections_pk_seq`
- `codec_stats_pk_seq`
- `inbound_rtp_stats_pk_seq`
- `outbound_rtp_stats_pk_seq`
- `media_source_stats_pk_seq`

## インデックス

パフォーマンス最適化のために以下のインデックスが作成されています：

```sql
-- connections テーブル
CREATE INDEX idx_connection_id ON connections(connection_id);
CREATE INDEX idx_connections_composite ON connections(channel_id, timestamp);

-- 各統計情報テーブル
CREATE INDEX idx_codec_composite ON codec_stats(channel_id, connection_id, timestamp);
CREATE INDEX idx_inbound_composite ON inbound_rtp_stats(channel_id, connection_id, timestamp);
CREATE INDEX idx_outbound_composite ON outbound_rtp_stats(channel_id, connection_id, timestamp);
CREATE INDEX idx_media_source_composite ON media_source_stats(channel_id, connection_id, timestamp);
```

## 注意事項

- `codec_stats` テーブルには UNIQUE 制約があるため、同じコーデック情報は重複して挿入されません
- すべてのタイムスタンプは UTC で記録されます
- `rtc_timestamp` は WebRTC の performance.timeOrigin + performance.now() の値（ミリ秒）です
- `outbound_rtp_stats` の `psnrSum` と `psnrMeasurements` フィールドは現在未実装です