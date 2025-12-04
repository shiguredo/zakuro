# Zakuro JSON-RPC API ドキュメント

Zakuro の HTTP サーバーは JSON-RPC 2.0 プロトコルを使用して API を提供します。

## エンドポイント

- **URL**: `http://localhost:<http-port>/rpc`
- **Method**: POST
- **Content-Type**: application/json

## 利用可能なメソッド

### GetVersion

Zakuro および関連ライブラリのバージョン情報を取得します。

#### リクエスト

```json
{
  "jsonrpc": "2.0",
  "method": "GetVersion",
  "id": 1
}
```

#### レスポンス

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "zakuro": "2025.3.0",
    "duckdb": "1.4.2",
    "sora_cpp_sdk": "2025.6.0",
    "libwebrtc": "143.7499.1.0",
    "boost": "1.89.0"
  }
}
```

### Query

DuckDB に対して SQL クエリを実行し、結果を取得します。

**注意**: このメソッドは `--duckdb-dir` オプションで DuckDB 出力が有効化されている場合のみ使用可能です。

#### リクエスト

```json
{
  "jsonrpc": "2.0",
  "method": "Query",
  "params": {
    "sql": "SELECT * FROM connection LIMIT 10"
  },
  "id": 1
}
```

#### レスポンス

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "rows": [
      {
        "pk": 1,
        "timestamp": "2024-12-26 12:34:56",
        "channel_id": "test-channel",
        "connection_id": "abc123",
        "session_id": "sess-001",
        "role": "sendrecv",
        "audio": true,
        "video": true,
        "websocket_connected": true,
        "datachannel_connected": true
      }
    ]
  }
}
```

### ListConnections

接続一覧を取得します。

**注意**: このメソッドは `--duckdb-dir` オプションで DuckDB 出力が有効化されている場合のみ使用可能です。

#### リクエスト

```json
{
  "jsonrpc": "2.0",
  "method": "ListConnections",
  "params": {
    "limit": 100
  },
  "id": 1
}
```

| パラメータ | 型 | デフォルト | 説明 |
|-----------|------|-----------|------|
| limit | integer | 100 | 取得する接続数の上限 (1-1000) |

#### レスポンス

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "connections": [
      {
        "timestamp": "2024-12-26 12:34:56",
        "channel_id": "test-channel",
        "connection_id": "abc123",
        "session_id": "sess-001",
        "role": "sendrecv",
        "audio": true,
        "video": true,
        "websocket_connected": true,
        "datachannel_connected": true
      }
    ],
    "total_count": 1
  }
}
```

## エラーレスポンス

JSON-RPC 2.0 仕様に従ったエラーレスポンスを返します。

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32601,
    "message": "Method not found",
    "data": "Unknown method: invalid/Method"
  }
}
```

### エラーコード

| コード | メッセージ | 説明 |
|--------|----------|------|
| -32700 | Parse error | JSON パースエラー |
| -32600 | Invalid Request | 無効なリクエスト |
| -32601 | Method not found | メソッドが見つからない |
| -32602 | Invalid params | 無効なパラメータ |
| -32603 | Internal error | 内部エラー |

## 使用例 (curl)

### バージョン情報の取得

```bash
curl -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "GetVersion",
    "id": 1
  }'
```
