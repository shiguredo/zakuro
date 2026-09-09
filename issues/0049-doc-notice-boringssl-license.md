# NOTICE の BoringSSL ライセンス表記を確認して追記する

- Created: 2026-09-09
- Completed: {YYYY-MM-DD}
- Branch: feature/update-notice-boringssl-license
- Polished: 2026-09-09

## 目的

zakuro は BoringSSL を直接利用しているが、`NOTICE` に BoringSSL のライセンス表記が無い。
`NOTICE` が再配布時に必要なライセンス表記を網羅しているか確認し、不足分を追記する。

## 現状

- `src/http_proxy.cpp` が `<openssl/ssl.h>` を include し、`SSL_CTX_new` / `SSL_set_tlsext_host_name` などを直接呼ぶ
- `CMakeLists.txt` は `OPENSSL_IS_BORINGSSL` を定義しており、`<openssl/ssl.h>` は `run.py` が shiguredo-webrtc-build からダウンロードする prebuilt libwebrtc に同梱された BoringSSL (`webrtc/include/third_party/boringssl/src/include`、`.vscode/c_cpp_properties.json` 参照) から解決されるため、実際に使われるのは OpenSSL ではなく BoringSSL である (issues/0009 参照)
- `src/http_proxy.cpp` は `boost/asio/ssl.hpp` も使い、Boost.Asio の SSL も同じく BoringSSL をリンクする
- `NOTICE` には Sora C++ SDK / Blend2D / CLI11 / Boost / Kosugi / OpenH264 の各ライセンス表記があるが、BoringSSL の表記が無い

## 設計方針

- BoringSSL のライセンス本文を BoringSSL リポジトリの `LICENSE` (Apache License 2.0 が主体。サポートコード用の BSD 系 (Go ライセンス) は libcrypto / libssl には適用されない) から取得して、`NOTICE` に既存セクションと同じ形式 (ライブラリ名 + ソース URL + ライセンス本文) で追記する
- BoringSSL のライセンスが Sora C++ SDK 側の NOTICE 等で既にカバーされ、zakuro 側の追記が不要である場合は、その根拠を明記した上で追記不要とする

## 完了条件

- `NOTICE` に BoringSSL のライセンス表記が追記されていること、または追記が不要である根拠が明記されていること
