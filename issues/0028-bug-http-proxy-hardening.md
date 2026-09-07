# HttpProxy の複数問題 (SSL_CTX null 未検査・URL パース・タイムアウト・body_limit・SSL_CTX 使い回し)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-http-proxy-hardening
- Polished: 2026-09-07

## 目的

`HttpProxy` に以下の複数問題があり、UI リバースプロキシとしての信頼性・堅牢性が低い。
まとめて修正する。TLS 検証欠落は別 issue で扱うが、それ以外の 5 点を本 issue で対応する。

- `SSL_CTX_new` の nullptr 未検査
- URL パーサーが `user:pass@host` / IPv6 リテラル / fragment を扱えない
- タイムアウトが各段階でリセットされ、総合タイムアウトが効かない
- `body_limit` を parser で持たず、暗黙の 8MB 制限と意図した 10MB 制限が食い違う
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
`host = "user"`, `port = "pass@example.com"` と誤パースする。結果として `async_resolve` が不正な host / port で失敗し、
本来接続すべき上流へ接続できない。`[ipv6]` リテラルも `url_str.find(':')` が IPv6 アドレスの区切り `:` に一致して
port 判定を誤検出する。fragment も区切り扱いされず、パスの無い URL では host または port に混入する。

### タイムアウト

`SetTimeout` は connect / handshake / write / read の各段階開始前に `expires_after(30s)` をリセットする。
resolve 段階 (`async_resolve`) にはタイムアウトが無い (`tcp_stream::expires_after` はソケット操作にしか効かないため)。
悪意あるサーバが各段階を 29 秒ずつ引き延ばすと、HTTPS では合計 2 分以上ハングし得る。
多数 VC が UI プロキシを叩くとリソース枯渇の原因になる。

### body_limit

`async_read(..., proxy_res_, ...)` を parser 明示なしで呼んでおり、
`response<dynamic_body>` の default body_limit に依存する。Boost.Beast の parser 既定値はレスポンスで 8MB のため、
本当に効いている制限は 8MB であり、`kMaxProxyResponseSize` (10MB) 以下でも 8MB 超のレスポンスは
`http::error::body_limit` で読み込みが失敗し、汎用の "Read error: body limit exceeded" の 502 になる。
`FinishResponse` の 10MB 検査 (body_size > kMaxProxyResponseSize) は読み込みが 8MB で先に失敗するため実質デッドコード。
なお parser で読み込みが制限されるため、単一レスポンスの読み込みが OOM につながることはない。

### SSL_CTX 使い回し無し

`HttpSession::AsyncHandleSimpleProxyRequest` はリクエストごとに `std::make_shared<HttpProxy>` して
新しい `ssl_ctx_` を生成する。上流への TCP 接続も毎回張り直すため TLS ハンドシェイクは接続ごとに必ず発生するが、
接続の keep-alive 化は本 issue の対象外とし、本 issue で削減するのは SSL_CTX の生成のみとする。
UI 資産 1 ページで数十リクエスト来る想定なら、毎回 SSL_CTX の生成 (CA パスの登録などを含む) が走る。

## 設計方針

- `SSL_CTX_new` の返り値を nullptr 検査し、失敗時 `SendErrorResponse` する
- URL パースを Boost.URL または独自の適切なパーサーに置き換える
  - `@` を含む userinfo・`[ipv6]` リテラル・fragment を含む URL を安全に扱えない場合は、パース時に明示エラーにする (誤パース後に `async_resolve` へ流さない)
  - Boost.URL を選ぶ場合は `buildbase.py` の Boost ビルドに `--with-url` の追加と `CMakeLists.txt` への `Boost::url` 追加が必要となり、全プラットフォームのビルドへ影響する。独自パーサーにはその制約が無いため、選定時に考慮する
- 全体 deadline を `steady_clock::now() + 30 秒` で保存し、`SetTimeout` は `expires_at(deadline)` を呼ぶ形に変更する
  - `expires_at` が効くのは `tcp_stream` のソケット操作のみ。`async_resolve` を含めて 30 秒で打ち切るには、`steady_timer` の期限で `resolver_.cancel()` するなど resolve 向けの対応も行う
- `boost::beast::http::response_parser<dynamic_body> parser; parser.body_limit(kMaxProxyResponseSize);` に切り替える
  - parser で上限が保証されるため、`FinishResponse` の 10MB 後置検査は削除する
- `ssl_ctx_` を `HttpServer` レベルで 1 個保持し、`HttpProxy` は参照で受け取る (同じ `ui_remote_url` なら再利用)
  - TLS 検証の追加 (issue 0009 で対応) も `ssl_ctx_` の構築箇所を変更するため、検証設定 (`set_verify_mode` / `host_name_verification`) を共有コンテキスト側に置く形で両立させる

## 完了条件

- `SSL_CTX_new` 失敗時にクラッシュせずエラーレスポンスを返すこと
- `user:pass@host` や `[ipv6]` を含む URL を安全に扱えること (エラー返却でも良い)
- 総合タイムアウト (resolve 含む) が 30 秒に収まること (悪意あるサーバのシミュレーションで検証)
- 10MB 超のレスポンスを送るサーバに接続しても、読み込みが OOM せず body_limit エラーで明示的に失敗すること
- 同じ `ui_remote_url` に対して `ssl_ctx_` が使い回されていること
