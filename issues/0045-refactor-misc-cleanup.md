# 小さな掃除まとめ (Xorshift ヘッダーガード名・iostream include・SendMessage スレッド・NOTICE)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-misc-cleanup
- Polished: {YYYY-MM-DD}

## 目的

以下の小さな改善指摘をまとめて対応する。単独では issue 化しにくいが、
放置するとレビュー時のノイズや将来の落とし穴になる項目。

## 現状

### Xorshift ヘッダーガード名

`src/xorshift.h` のインクルードガードが `RANDOM_H_`。ヘッダー名は `xorshift.h` なのに整合していない。
別のヘッダーで `RANDOM_H_` を使うと衝突する可能性。

### game_key_core.h の iostream 直接 include

`src/game/game_key_core.h` が `#include <iostream>` を持ち、`std::cerr` をヘッダー内実装で使っている。
ヘッダーで iostream を include するとインクルード先の全 TU に持ち込まれてコンパイル時間が膨らむ。
実装を `.cpp` に切り出し、ヘッダーから iostream を外す。

### VirtualClient::SendMessage のスレッド安全性

`src/virtual_client.cpp` の `VirtualClient::SendMessage` は現状 ScenarioPlayer::OnNext (ioc thread) からしか呼ばれないため
偶然安全に動くが、API から見て呼び出しスレッド制約が明示されていない。
将来別スレッドから呼ばれると Sora SDK 側の `SoraSignaling::SendDataChannel` の内部状態にレースを起こす可能性がある。

### NOTICE の OpenSSL 表記

`src/http_proxy.cpp` は `<openssl/ssl.h>` を直接 include し `SSL_CTX_new` などを呼ぶが、
`NOTICE` に OpenSSL / BoringSSL のライセンス表記が無い。webrtc の NOTICE がカバーしているか要確認。

### FileRotatingLogSink のパスハードコード

`src/main.cpp` の `FileRotatingLogSink("./", "webrtc_logs", ...)` はカレントディレクトリ依存。
CI や systemd から起動すると想定外の場所にログが出る。書き込み権限が無いと `return 1;` で終わる。
`--log-dir` オプション化。

## 設計方針

- Xorshift のガード名を `XORSHIFT_H_` に変更
- game_key_core.h の実装を `.cpp` に切り出し、ヘッダーから iostream を外す
- `VirtualClient::SendMessage` を `boost::asio::post(*config_.sora_config.io_context, [...] { ... })` でラップするか、少なくとも呼び出しスレッド制約をコメントで明示
- `NOTICE` を確認し、必要なら OpenSSL / BoringSSL のライセンスを追記
- `--log-dir` / `--log-prefix` CLI オプションを追加

## 完了条件

- 上記 5 点が全て対応済みになっていること
- ビルドとテストが通ること
