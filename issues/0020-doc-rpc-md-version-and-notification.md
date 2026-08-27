# doc/RPC.md のバージョン例と Notification 仕様の記述を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-rpc-md-version-and-notification
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`doc/RPC.md` の `GetVersion` レスポンス例が実装と全項目で乖離しており、
また CHANGES.md で追加宣言した「Notification (id なしリクエスト) の 204 No Content 応答」の記述が RPC.md に無い。
実装と一致させる。

## 現状

### レスポンス例のバージョンが全項目古い

`doc/RPC.md` の `GetVersion` レスポンス例に載っているバージョンは以下。

- `"zakuro": "2025.3.0"`
- `"sora_cpp_sdk": "2025.6.0"`
- `"libwebrtc": "143.7499.1.0"`
- `"boost": "1.89.0"`

実際の develop の値。

- `zakuro`: `VERSION` は `2026.1.0-canary.8` (次リリースは `2026.1.0` 想定)
- `sora_cpp_sdk`: `DEPS` の `SORA_CPP_SDK_VERSION` は `2026.2.0-canary.19` (issue 0002 で `2026.2.1` に更新予定)
- `libwebrtc`: `DEPS` の `WEBRTC_BUILD_VERSION` は `m150.7871.3.0`
- `boost`: `DEPS` の `BOOST_VERSION` は `1.91.0`

全項目が異なる。

### Notification (204 No Content) の説明が無い

`CHANGES.md ## develop` に `[ADD] JSON-RPC 2.0 の Notification（id なしリクエスト）に対応する` (204 No Content を返す)
と明記されている。実装は `src/http_server.cpp` (`HandleJsonRpcRequest`) と `src/json_rpc.cpp` (`Process`) にある。
`doc/RPC.md` には Notification に関する説明が一切ない。

### エラーコード表の網羅性

`doc/RPC.md` のエラーコード表に `-32700` / `-32600` / `-32601` / `-32603` が列挙されているが、
JSON-RPC 2.0 標準の `-32602 (Invalid params)` が無い。現状の実装では返さないが、将来対応時のために言及が望ましい。

## 設計方針

- レスポンス例のバージョンを、リリース版のバージョンに合わせるか、`X.Y.Z` プレースホルダで例示する
  (canary 番号を書くと canary 変更のたびに RPC.md を更新することになるので、プレースホルダが実用的)
- Notification に関する節を追加し、以下を明記
  - `id` フィールドが無いリクエストは Notification として扱う
  - Notification の場合、レスポンスは返さず 204 No Content を返す
  - Notification のエラー時もレスポンスを返さない
- `-32602` を「現状は使用しない」と明示するか、想定される将来利用として記載

## 完了条件

- `doc/RPC.md` のレスポンス例が実装と齟齬なく確認できる形になっていること
- Notification の仕様が RPC.md に明記されていること
- エラーコード表が実装と一致していること (`-32602` の扱いも明示)
