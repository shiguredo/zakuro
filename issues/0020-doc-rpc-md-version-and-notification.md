# doc/RPC.md のバージョン例と Notification 仕様の記述を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-rpc-md-version-and-notification
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`doc/RPC.md` の `GetVersion` レスポンス例が実装と全項目で乖離しており、
また CHANGES.md で追加宣言した「Notification (id なしリクエスト) の 204 No Content 応答」の記述が RPC.md に無い。
実装と矛盾しない形、かつ実値は `GetVersion` のレスポンスで確認できる形に修正する。

## 現状

### レスポンス例のバージョンが全項目古い

`doc/RPC.md` の `GetVersion` レスポンス例に載っているバージョンは以下。

- `"zakuro": "2025.3.0"`
- `"sora_cpp_sdk": "2025.6.0"`
- `"libwebrtc": "143.7499.1.0"`
- `"boost": "1.89.0"`

実際の develop の値は以下。

- `zakuro`: `VERSION` は `2026.1.0-canary.8` (次リリースは `2026.1.0` 想定)
- `sora_cpp_sdk`: `DEPS` の `SORA_CPP_SDK_VERSION` は `2026.2.0-canary.19` (issue 0002 で `2026.2.1` に更新予定)
- `libwebrtc`: `GetVersion` が返すのは `ZakuroVersion::GetWebRTCVersion` の `WEBRTC_BUILD_VERSION` で、
  値は `150.7871.3.0` (webrtc-build の VERSIONS ファイル由来のため `m` プレフィックス無し。`DEPS` では `m150.7871.3.0`)
- `boost`: `DEPS` の `BOOST_VERSION` は `1.91.0`

全項目が異なる。

### Notification (204 No Content) の説明が無い

`CHANGES.md ## develop` に `[ADD] JSON-RPC 2.0 の Notification（id なしリクエスト）に対応する` (204 No Content を返す)
と明記されている。実装は `src/http_server.cpp` (`HttpSession::HandleJsonRpcRequest`) と `src/json_rpc.cpp` (`JsonRpcHandler::Process`) にある。
`doc/RPC.md` には Notification に関する説明が一切ない。

### エラーコード表の網羅性

`doc/RPC.md` のエラーコード表に `-32700` / `-32600` / `-32601` / `-32603` が列挙されているが、
JSON-RPC 2.0 標準の `-32602 (Invalid params)` が無い。現状の実装では返さないが、将来対応時のために言及が望ましい。

## 設計方針

- レスポンス例のバージョンは `X.Y.Z` プレースホルダで例示する
  - canary 番号を書くと canary 変更のたびに RPC.md を更新することになるため
  - 例示がプレースホルダであること、実際の値は `GetVersion` のレスポンスで確認できることを本文に明記する
  - libwebrtc の実値が `m150.7871.3.0` ではなく `150.7871.3.0` の形式であることも注記に含める
- Notification に関する節を追加し、以下を明記
  - `id` フィールドが無いリクエストは Notification として扱う (`JsonRpcHandler::Process` の `obj.contains("id")` で判定)
  - `id` が `null` のリクエストは Notification ではなく通常リクエストとして扱い、`id: null` 付きで応答する
  - Notification の場合、レスポンスは返さず 204 No Content を返す (`HttpSession::HandleJsonRpcRequest` が `std::nullopt` に対して 204 を返す)
  - Notification のエラー時もレスポンスを返さない (`JsonRpcHandler::Process` のエラーパスである jsonrpc フィールド不正・method フィールド不正・メソッド不明・内部エラーの各経路が `is_notification` なら `std::nullopt` を返す)
- `-32602` は「JSON-RPC 2.0 標準のエラーコードだが現在の実装では使用しない」旨の注記をエラーコード表の下に追加する
  (表には現在実装が返すコードのみを記載する)

## 完了条件

- `doc/RPC.md` のレスポンス例がプレースホルダで例示され、実値は `GetVersion` のレスポンスで確認できる旨が明記されていること
- Notification の仕様が RPC.md に明記されていること (id フィールド無し → 204 No Content、エラー時もレスポンス無し)
- エラーコード表が実装と一致していること (実装が返す `-32700` / `-32600` / `-32601` / `-32603` のみで、`-32602` の扱いも明示)
