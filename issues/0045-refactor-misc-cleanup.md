# 小さな掃除まとめ (Xorshift ヘッダーガード名・iostream include・SendMessage スレッド・ログ出力先オプション)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-misc-cleanup
- Polished: 2026-09-09

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

### FileRotatingLogSink のパスハードコード

`src/main.cpp` の `FileRotatingLogSink("./", "webrtc_logs", ...)` はカレントディレクトリ依存。
CI や systemd から起動すると想定外の場所にログが出る。書き込み権限が無いと `return 1;` で終わる。
`--log-dir` オプション化。

## 設計方針

- Xorshift のガード名を `XORSHIFT_H_` に変更する
- `src/game/game_key_core.cpp` を新規作成して実装を移し、`CMakeLists.txt` の `target_sources` に追加する。
  ヘッダーから iostream を外す (`std::cerr` の出力は .cpp 側に残す)。
  なお `src/game/game_key_core.h` は issues/0006 (keys_ の mutex 追加) も変更するため、実装時に 0006 の
  反映状況を確認してから進める
- `VirtualClient::SendMessage` を `boost::asio::post(*config_.sora_config.io_context, ...)` で ioc スレッドへ
  委譲し、呼び出しスレッド制約を不要にする。post 先のラムダでは `signaling_` / `closing_` を再確認し、
  `shared_from_this` (または `weak_from_this`) でオブジェクト寿命を保証すること
  (raw `this` をキャプチャした post は issues/0024 が排除する失敗パターンと同じため禁止)
- `--log-dir` / `--log-prefix` CLI オプションを追加する (デフォルトは現行と同じ `./` / `webrtc_logs`)。
  ログシンクの生成・破棄は issues/0010 (RAII 化) も変更するため、実装時に 0010 の反映状況を確認してから進める

## 完了条件

- 上記 4 点が全て対応済みになっていること
- ビルドとテストが通ること
