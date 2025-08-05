# Zakuro JSON-RPC API ドキュメント

Zakuro の HTTP サーバーは JSON-RPC 2.0 プロトコルを使用して、WebRTC 統計情報へのアクセスと分析機能を提供します。

## エンドポイント

- **URL**: `http://localhost:<http-port>/rpc`
- **Method**: POST
- **Content-Type**: application/json

## 利用可能なメソッド

### 1. version

Zakuro および関連ライブラリのバージョン情報を取得します。

#### リクエスト

```json
{
  "jsonrpc": "2.0",
  "method": "version",
  "id": 1
}
```

#### レスポンス

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "zakuro": "2025.3.0-canary0",
    "duckdb": "1.3.2",
    "sora_cpp_sdk": "2025.4.0",
    "libwebrtc": "m138.7204.0.1",
    "boost": "1.88.0"
  }
}
```

### 2. query

DuckDB に対して SQL クエリを実行し、統計情報を取得します。

#### リクエスト

```json
{
  "jsonrpc": "2.0",
  "method": "query",
  "params": {
    "sql": "SELECT * FROM connection ORDER BY timestamp DESC LIMIT 10"
  },
  "id": 2
}
```

#### レスポンス

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "rows": [
      {
        "pk": 1,
        "timestamp": "2025-08-01 10:30:45",
        "channel_id": "zakuro-test",
        "connection_id": "ABC123",
        "session_id": "xyz789",
        "audio": true,
        "video": true,
        "websocket_connected": true,
        "datachannel_connected": false
      }
    ],
    "row_count": 1,
    "column_count": 9,
    "columns": [
      {"name": "pk", "type": "BIGINT"},
      {"name": "timestamp", "type": "TIMESTAMP"},
      {"name": "channel_id", "type": "VARCHAR"},
      {"name": "connection_id", "type": "VARCHAR"},
      {"name": "session_id", "type": "VARCHAR"},
      {"name": "audio", "type": "BOOLEAN"},
      {"name": "video", "type": "BOOLEAN"},
      {"name": "websocket_connected", "type": "BOOLEAN"},
      {"name": "datachannel_connected", "type": "BOOLEAN"}
    ]
  }
}
```

## DuckDB テーブル構造

### zakuro テーブル

Zakuro プロセスの実行情報を記録します。

| カラム名 | 型 | 説明 |
|---------|-----|------|
| version | VARCHAR | Zakuro バージョン |
| environment | VARCHAR | 実行環境 |
| webrtc_version | VARCHAR | WebRTC バージョン |
| sora_cpp_sdk_version | VARCHAR | Sora C++ SDK バージョン |
| boost_version | VARCHAR | Boost バージョン |
| cli11_version | VARCHAR | CLI11 バージョン |
| cmake_version | VARCHAR | CMake バージョン |
| blend2d_version | VARCHAR | Blend2D バージョン |
| openh264_version | VARCHAR | OpenH264 バージョン |
| yaml_cpp_version | VARCHAR | yaml-cpp バージョン |
| duckdb_version | VARCHAR | DuckDB バージョン |
| config_mode | VARCHAR | 設定モード（'ARGS' または 'YAML'） |
| config_json | JSON | 設定内容（JSON 形式） |
| start_timestamp | TIMESTAMP | プロセス開始時刻 |
| stop_timestamp | TIMESTAMP | プロセス終了時刻 |

### zakuro_scenario テーブル

Zakuro シナリオ情報を記録します。

| カラム名 | 型 | 説明 |
|---------|-----|------|
| vcs | INTEGER | 仮想クライアント数 |
| duration | DOUBLE | 実行時間 |
| repeat_interval | DOUBLE | 繰り返し間隔 |
| max_retry | INTEGER | 最大リトライ回数 |
| retry_interval | DOUBLE | リトライ間隔 |
| sora_signaling_urls | VARCHAR[] | Sora シグナリング URL リスト |
| sora_channel_id | VARCHAR | Sora チャンネル ID |
| sora_role | VARCHAR | Sora ロール |

### connection テーブル

接続情報を記録します。

| カラム名 | 型 | 説明 |
|---------|-----|------|
| pk | BIGINT | 主キー（自動採番） |
| timestamp | TIMESTAMP | 記録時刻 |
| channel_id | VARCHAR | Sora チャンネル ID |
| connection_id | VARCHAR | Sora 接続 ID |
| session_id | VARCHAR | セッション ID |
| audio | BOOLEAN | 音声トラックの有無 |
| video | BOOLEAN | 映像トラックの有無 |
| websocket_connected | BOOLEAN | WebSocket 接続状態 |
| datachannel_connected | BOOLEAN | DataChannel 接続状態 |

### rtc_stats_codec テーブル

コーデック情報を記録します。

| カラム名 | 型 | 説明 |
|---------|-----|------|
| pk | BIGINT | 主キー（自動採番） |
| timestamp | TIMESTAMP | 記録時刻 |
| channel_id | VARCHAR | Sora チャンネル ID |
| session_id | VARCHAR | セッション ID |
| connection_id | VARCHAR | Sora 接続 ID |
| rtc_timestamp | DOUBLE | RTC タイムスタンプ |
| type | VARCHAR | 統計情報タイプ |
| id | VARCHAR | 統計情報 ID |
| mime_type | VARCHAR | MIME タイプ（例: video/H264） |
| payload_type | BIGINT | ペイロードタイプ |
| clock_rate | BIGINT | クロックレート |
| channels | BIGINT | チャンネル数（音声のみ） |
| sdp_fmtp_line | VARCHAR | SDP fmtp 行 |

### rtc_stats_inbound_rtp テーブル

受信 RTP ストリームの統計情報を記録します。

主要なカラム：

- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- RTP 情報: ssrc, kind (audio/video), transport_id, codec_id
- パケット統計: packets_received, packets_lost, bytes_received
- 品質指標: jitter, frames_decoded, frames_dropped
- ビデオ統計: frame_width, frame_height, frames_per_second
- オーディオ統計: audio_level, total_audio_energy, concealed_samples

### rtc_stats_outbound_rtp テーブル

送信 RTP ストリームの統計情報を記録します。

主要なカラム：

- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- RTP 情報: ssrc, kind (audio/video), transport_id, codec_id
- パケット統計: packets_sent, bytes_sent
- エンコード統計: frames_encoded, key_frames_encoded, total_encode_time
- 品質制限情報: quality_limitation_reason, quality_limitation_duration_*
- ビデオ統計: frame_width, frame_height, frames_per_second

### rtc_stats_media_source テーブル

メディアソースの統計情報を記録します。

主要なカラム：

- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- メディア情報: track_identifier, kind (audio/video)
- オーディオ統計: audio_level, total_audio_energy, echo_return_loss
- ビデオ統計: width, height, frames, frames_per_second

### rtc_stats_remote_inbound_rtp テーブル

リモート受信 RTP ストリームの統計情報を記録します。

主要なカラム：

- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- RTP 情報: ssrc, kind (audio/video), transport_id, codec_id
- パケット統計: packets_received, packets_lost, jitter
- リモート統計: local_id, round_trip_time, total_round_trip_time, fraction_lost

### rtc_stats_remote_outbound_rtp テーブル

リモート送信 RTP ストリームの統計情報を記録します。

主要なカラム：

- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- RTP 情報: ssrc, kind (audio/video), transport_id, codec_id
- パケット統計: packets_sent, bytes_sent
- リモート統計: local_id, remote_timestamp, reports_sent, round_trip_time

### rtc_stats_data_channel テーブル

データチャンネルの統計情報を記録します。

主要なカラム：

- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- チャンネル情報: label, protocol, data_channel_identifier, state
- メッセージ統計: messages_sent, bytes_sent, messages_received, bytes_received

## クエリ例

### 最新の接続情報を取得

```sql
SELECT * FROM connection 
ORDER BY timestamp DESC 
LIMIT 10
```

### 特定のチャンネルのビデオ受信統計を取得

```sql
SELECT 
  timestamp,
  connection_id,
  frames_decoded,
  frames_dropped,
  frame_width,
  frame_height,
  frames_per_second
FROM rtc_stats_inbound_rtp
WHERE channel_id = 'your-channel-id' 
  AND kind = 'video'
ORDER BY timestamp DESC
```

### 送信品質制限の統計を取得

```sql
SELECT 
  timestamp,
  connection_id,
  quality_limitation_reason,
  quality_limitation_duration_cpu,
  quality_limitation_duration_bandwidth,
  quality_limitation_duration_other
FROM rtc_stats_outbound_rtp
WHERE channel_id = 'your-channel-id'
  AND quality_limitation_reason != 'none'
ORDER BY timestamp DESC
```

### 接続別のパケットロス率を計算

```sql
SELECT 
  connection_id,
  AVG(CAST(packets_lost AS DOUBLE) / 
      NULLIF(packets_received + packets_lost, 0) * 100) AS packet_loss_rate
FROM rtc_stats_inbound_rtp
WHERE timestamp > CURRENT_TIMESTAMP - INTERVAL '5 minutes'
GROUP BY connection_id
```

## エラーレスポンス

JSON-RPC 2.0 仕様に従ったエラーレスポンスを返します。

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32602,
    "message": "Invalid params",
    "data": "params.sql must be a string"
  }
}
```

### エラーコード

- `-32600`: 無効なリクエスト
- `-32601`: メソッドが見つからない
- `-32602`: 無効なパラメータ
- `-32603`: 内部エラー（クエリ実行エラーなど）

## 使用例 (curl)

### バージョン情報の取得

```bash
curl -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "version",
    "id": 1
  }'
```

### SQL クエリの実行

```bash
curl -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "query",
    "params": {
      "sql": "SELECT COUNT(*) as total_connections FROM connection"
    },
    "id": 2
  }'
```

### データチャンネル統計情報の取得

```bash
curl -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "query",
    "params": {
      "sql": "SELECT * FROM rtc_stats_data_channel WHERE channel_id = '\''your-channel-id'\'' ORDER BY timestamp DESC LIMIT 10"
    },
    "id": 3
  }'
```
