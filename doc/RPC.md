# Zakuro JSON-RPC API ドキュメント

Zakuro の HTTP サーバーは JSON-RPC 2.0 プロトコルを使用して API を提供します。

## エンドポイント

- **URL**: `http://localhost:<http-port>/rpc`
- **Method**: POST
- **Content-Type**: application/json

## Notification

JSON-RPC 2.0 の Notification (`id` フィールドが無いリクエスト) に対応しています。

- `id` フィールドが無いリクエストは Notification として扱う
- `id` が `null` のリクエストは Notification ではなく通常のリクエストとして扱い、`"id": null` 付きで応答する
- Notification の場合、JSON-RPC のレスポンスは返さず HTTP ステータス 204 No Content (ボディ空) を返す
- Notification でエラーが起きても、同様に JSON-RPC のレスポンスは返さず 204 No Content を返す

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

レスポンス例のバージョン文字列はプレースホルダです。実際の値は `GetVersion` のレスポンスで確認してください。
`libwebrtc` は webrtc-build の `WEBRTC_BUILD_VERSION` 由来のため、`m` プレフィックス無しの形式 (例: `150.7871.3.1`) です。

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "zakuro": "X.Y.Z",
    "sora_cpp_sdk": "X.Y.Z",
    "libwebrtc": "X.Y.Z.W",
    "boost": "X.Y.Z"
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

現在の実装が返すエラーコードは次のとおりです。

| コード | メッセージ | 説明 |
| -------- | ---------- | ------ |
| -32700 | Parse error | JSON パースエラー |
| -32600 | Invalid Request | 無効なリクエスト |
| -32601 | Method not found | メソッドが見つからない |
| -32603 | Internal error | 内部エラー |

JSON-RPC 2.0 標準には `-32602` (Invalid params) もありますが、現在の実装では使用しません。

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

### Notification (`id` フィールド無し)

```bash
curl -i -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "GetVersion"
  }'
```

レスポンスは HTTP ステータス 204 No Content で、ボディは空です。
