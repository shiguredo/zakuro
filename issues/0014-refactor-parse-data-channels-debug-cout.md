# ParseDataChannels の std::cout デバッグ痕跡を除去する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-parse-data-channels-debug-cout
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` から呼ばれる `ParseDataChannels` に、開発中の一時デバッグと思われる
`std::cout << __LINE__ << std::endl;` が 10 箇所残っている。
この出力はプロジェクトのログ経路 (`RTC_LOG`) に載らず、行番号だけでは失敗理由が伝わらない。
すべて `RTC_LOG(LS_ERROR)` で意味のある英語メッセージに置き換えて痕跡を除去する。

## 現状

`src/zakuro.cpp` の `ParseDataChannels` のエラーパスは 12 箇所あり、そのうち 10 箇所は
`std::cout << __LINE__ << std::endl; return false;` の形で行番号だけを stdout に出している。

- `dcs.is_array()` が false のとき
- 各 channel が object でないとき
- `label` が欠落・型不正のとき
- `direction` が欠落・型不正のとき
- `interval` が数値でない・0 以下のとき
- `size-min` が数値でない・範囲外のとき

残り 2 箇所 (`size-max` が数値でない・範囲外のとき) は `std::cout` を出さず、
無出力のまま `return false;` している。

`std::cout` はプロジェクトのログ経路に載らない。`main.cpp` では webrtc::LogMessage
の `LogToDebug` と `FileRotatingLogSink` (`./webrtc_logs` ファイル) がログ経路であり、
`RTC_LOG` はそこに流れる。`std::cout` は stdout に出るだけなので、`--log-level` や
ログファイルからは観測できず、負荷試験時に行番号が大量に出ても原因究明の役に立たない。
メッセージを出すなら AGENTS.md の「ログメッセージは全て英語にすること」に従い、
英語で意味のある内容にすべきである。

## 設計方針

- `std::cout << __LINE__ << std::endl;` を 10 箇所すべて削除し、`RTC_LOG(LS_ERROR)` に置き換える
- `size-max` の 2 箇所 (無出力のエラーパス) にも `RTC_LOG(LS_ERROR)` を追加し、
  `ParseDataChannels` のエラーパスすべてで原因がログに残るようにする
- メッセージは `"ParseDataChannels: <該当項目> is invalid"` を基本とし、
  具体的な理由を含める ("label is missing", "interval must be positive",
  "size-min out of range" 等)
- `src/zakuro.cpp` に `#include <rtc_base/logging.h>` を追加する (`RTC_LOG` の利用に必要)
- 例外的に「一時的にデバッグしたい」用途で残す必要はない。すべて意味のあるメッセージに置換する
- 呼び出し側 `Zakuro::Run` の `std::cerr` (`failed to parse DataChannels`) は既存の
  エラー処理であり、本 issue では変更しない
- `interval` ブロックは issue 0013 (到達不能コード) と同一ブロックを編集するため、
  実装時に 1 つのブランチで 2 件まとめて対応してよい

## 完了条件

- `ParseDataChannels` に `std::cout` が 0 件になること (`git grep -n 'std::cout' src/zakuro.cpp`)
- エラーパスすべてで具体的な英語メッセージが `RTC_LOG(LS_ERROR)` に出ること
- 不正な `sora-data-channels` を渡した際、ログから原因が特定できること
