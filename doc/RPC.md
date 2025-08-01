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
    "zakuro": "2025.3.0-dev",
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
    "sql": "SELECT * FROM connections ORDER BY timestamp DESC LIMIT 10"
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

### connections テーブル

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

### codec_stats テーブル

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

### inbound_rtp_stats テーブル

受信 RTP ストリームの統計情報を記録します。

主要なカラム：
- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- RTP 情報: ssrc, kind (audio/video), transport_id, codec_id
- パケット統計: packets_received, packets_lost, bytes_received
- 品質指標: jitter, frames_decoded, frames_dropped
- ビデオ統計: frame_width, frame_height, frames_per_second
- オーディオ統計: audio_level, total_audio_energy, concealed_samples

### outbound_rtp_stats テーブル

送信 RTP ストリームの統計情報を記録します。

主要なカラム：
- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- RTP 情報: ssrc, kind (audio/video), transport_id, codec_id
- パケット統計: packets_sent, bytes_sent
- エンコード統計: frames_encoded, key_frames_encoded, total_encode_time
- 品質制限情報: quality_limitation_reason, quality_limitation_duration_*
- ビデオ統計: frame_width, frame_height, frames_per_second

### media_source_stats テーブル

メディアソースの統計情報を記録します。

主要なカラム：
- 基本情報: pk, timestamp, channel_id, session_id, connection_id
- メディア情報: track_identifier, kind (audio/video)
- オーディオ統計: audio_level, total_audio_energy, echo_return_loss
- ビデオ統計: width, height, frames, frames_per_second

## クエリ例

### 最新の接続情報を取得

```sql
SELECT * FROM connections 
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
FROM inbound_rtp_stats
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
FROM outbound_rtp_stats
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
FROM inbound_rtp_stats
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
      "sql": "SELECT COUNT(*) as total_connections FROM connections"
    },
    "id": 2
  }'
```