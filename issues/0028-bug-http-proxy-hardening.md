# HttpProxy の複数問題 (SSL_CTX null 未検査・URL パース・タイムアウト・body_limit・SSL_CTX 使い回し)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-http-proxy-hardening
- Polished: {YYYY-MM-DD}

## 目的

`HttpProxy` に以下の複数問題があり、UI リバースプロキシとしての信頼性・堅牢性が低い。
まとめて修正する。TLS 検証欠落は別 issue で扱うが、それ以外の 5 点を本 issue で対応する。

- `SSL_CTX_new` の nullptr 未検査
- URL パーサーが `user:pass@host` / IPv6 リテラル / fragment を扱えない
- タイムアウトが各段階でリセットされ、総合タイムアウトが効かない
- `body_limit` を parser で持たず、dynamic_body に無制限に読み込む
- リクエスト毎に SSL_CTX / TCP 接続を新規生成する

## 現状

### SSL_CTX_new nullptr

`src/http_proxy.cpp` の `OnResolve` の `use_ssl_` 分岐:

```cpp
SSL_CTX* handle = ::SSL_CTX_new(::TLS_method());
SSL_CTX_set_min_proto_version(handle, TLS1_2_VERSION);
```

`SSL_CTX_new` が nullptr を返しても検査せず、以降の API 呼び出しで NPE。

### URL パーサー

`HttpProxy::ParseUrl` は `find(':')` と `find('/')` のみで判定。`@` を扱わず、`https://user:pass@example.com/` は
`host = "user"`, `port = "pass@example.com"` と誤パース。`[ipv6]` リテラルも `host_.find(':')` で port 判定を誤検出する。

### タイムアウト

`SetTimeout` は各段階 (resolve / connect / handshake / write / read) で `expires_after(30s)` をリセット。
悪意あるサーバが各段階を 29 秒ずつ引き延ばすと、合計 2 分以上ハングし得る。
多数 VC が UI プロキシを叩くとリソース枯渇の原因になる。

### body_limit

`async_read(..., proxy_res_, ...)` を parser 明示なしで呼んでおり、
`response<dynamic_body>` の default body_limit に依存する。大きなレスポンスで OOM リスク。
現状の 10MB 制限は read 後に body_size を検査するもので、read 中の OOM を防げない。

### SSL_CTX 使い回し無し

`HttpSession` ごとに `std::make_shared<HttpProxy>` して新しい `ssl_ctx_` を生成。
UI 資産 1 ページで数十リクエスト来る想定なら、毎回 TLS ハンドシェイクとコンテキスト生成が走る。

## 設計方針

- `SSL_CTX_new` の返り値を nullptr 検査し、失敗時 `SendErrorResponse` する
- URL パースを Boost.URL または独自の適切なパーサーに置き換える (最低限 `@` を含む URL は明示エラー)
- 全体 deadline を `steady_clock::now() + N s` で保存し、`SetTimeout` は `expires_at(deadline)` を呼ぶ形に変更
- `boost::beast::http::response_parser<dynamic_body> parser; parser.body_limit(kMaxProxyResponseSize);` に切り替える
- `ssl_ctx_` を `HttpServer` レベルで 1 個保持し、`HttpProxy` は参照で受け取る (同じ `ui_remote_url` なら再利用)

## 完了条件

- `SSL_CTX_new` 失敗時にクラッシュせずエラーレスポンスを返すこと
- `user:pass@host` や `[ipv6]` を含む URL を安全に扱えること (エラー返却でも良い)
- 総合タイムアウトが 30 秒に収まること (悪意あるサーバのシミュレーションで検証)
- 巨大レスポンス受信中に OOM しないこと (10MB 以上を送るサーバに接続して検証)
- 同じ `ui_remote_url` に対して `ssl_ctx_` が使い回されていること
