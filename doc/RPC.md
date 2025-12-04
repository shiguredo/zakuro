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
    "sora_cpp_sdk": "2025.6.0",
    "libwebrtc": "m143.7499.1.0",
    "boost": "1.88.0"
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
