# HttpProxy の TLS サーバー証明書検証が完全に無効になっている

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-http-proxy-tls-verify
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`HttpProxy` (`--ui` の UI リバースプロキシで使用) が SSL コンテキストに verify モードを設定しておらず、
自己署名・期限切れ・ホスト名不一致の証明書でも TLS ハンドシェイクが通ってしまう。
MITM 検知不可のセキュリティ問題を修正する。

## 現状

`src/http_proxy.cpp` の `HttpProxy::OnResolve` の `use_ssl_` 分岐は以下を実行している。

- `SSL_CTX_new(::TLS_method())` で raw handle を作成
- `SSL_CTX_set_min_proto_version` / `SSL_CTX_set_max_proto_version` で TLS 1.2 / 1.3 のみ許可
- `boost::asio::ssl::context` を raw handle でラップ
- `ssl_ctx_->set_default_verify_paths()` を呼ぶ
- `ssl_ctx_->set_options(...)` で default_workarounds など設定
- `SSL_set_tlsext_host_name(...)` で SNI を設定

しかし `set_verify_mode(boost::asio::ssl::verify_peer)` に相当する呼び出しが一切なく、
デフォルトの検証モードは `SSL_VERIFY_NONE` のため、証明書検証が実行されない。
`set_default_verify_paths()` は CA バンドルのパスを追加するだけで、検証を有効化する API ではない。

本プロジェクトの SSL 実装は OpenSSL ではなく BoringSSL である
(`CMakeLists.txt` が `OPENSSL_IS_BORINGSSL` を定義し、`buildbase.py` が BoringSSL を
OpenSSL 互換としてインストールしている)。
本 issue で使用する OpenSSL 系 API (`set_verify_mode` / `SSL_set1_host` /
`set_default_verify_paths`) はすべて BoringSSL が提供しており、Boost.Asio の
`host_name_verification` も BoringSSL が持つ `X509_check_host` / `X509_check_ip_asc` を
使うため使用可能である。
`set_default_verify_paths()` の参照先は BoringSSL では macOS が `/etc/ssl/cert.pem`、
Ubuntu が `/etc/ssl/certs` の固定パスであり、`SSL_CERT_FILE` / `SSL_CERT_DIR` 環境変数で
上書きできる (OpenSSL のビルド時パスではない)。

`--ui-remote-url` のデフォルト値は `https://zakuro-ui.shiguredo.app/` で HTTPS。
ユーザーが本オプションを利用した瞬間、UI プロキシ経由の通信は MITM に対して無防備になる。

## 設計方針

`ssl_ctx_` 構築後、以下を追加する。

- `ssl_ctx_->set_verify_mode(boost::asio::ssl::verify_peer);`
- ホスト名検証のため、以下のいずれか
  - Boost 1.73 以降: `ssl_ctx_->set_verify_callback(boost::asio::ssl::host_name_verification(host_));`
  - もしくは `SSL_set1_host(ssl_stream_->native_handle(), host_.c_str());` を SNI 設定の直後に呼ぶ

既存の `--insecure` は Sora の signaling 接続用であり、本プロキシには適用されない。
HTTP プロキシ用の検証スキップオプションを新設するかは別 issue で議論する。
本 issue の範囲は「デフォルトで検証を有効にする」ことに絞る。

## 完了条件

- `HttpProxy` を経由した HTTPS 接続で、自己署名証明書・期限切れ証明書・ホスト名不一致証明書のサーバーへ
  接続すると、ハンドシェイクが失敗して 502 Bad Gateway が返ること
- 正常な公開証明書 (Let's Encrypt など) のサーバーへの接続は成功すること
- macOS は `/etc/ssl/cert.pem`、Ubuntu は `/etc/ssl/certs` のシステム CA が読み込まれ、
  上記の公開証明書の検証が通ること
