# ParseDataChannels の std::cout デバッグ痕跡を除去する

- Created: 2026-08-27
- Completed: 2026-09-29
- Branch: feature/fix-parse-data-channels-unreachable-code
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` から呼ばれる `ParseDataChannels` に、開発中の一時デバッグと思われる
`std::cout << __LINE__ << std::endl;` が多数残っている。
この出力はプロジェクトのログ経路 (`RTC_LOG`) に載らず、行番号だけでは失敗理由が伝わらない。
すべて `RTC_LOG(LS_ERROR)` で意味のある英語メッセージに置き換えて痕跡を除去する。

## 現状

`src/zakuro.cpp` の `ParseDataChannels` のエラーパス (`return false;`) は 22 箇所あり、
そのうち 19 箇所は `std::cout << __LINE__ << std::endl; return false;` の形で行番号だけを
stdout に出している (`git show HEAD:src/zakuro.cpp | grep -c 'std::cout << __LINE__'` が 19)。

- `data_channels` が配列でないとき
- 各 channel が object でないとき
- `label` が欠落・型不正のとき
- `direction` が欠落・型不正のとき
- `interval` が数値でない・整数でない・0 以下のとき
- `size-min` が数値でない・整数でない・範囲外のとき
- `ordered` / `max_packet_life_time` / `max_retransmits` / `protocol` / `compress` が型不正のとき

残り 3 箇所 (`size-max` が数値でない・整数でない・範囲外のとき) は `std::cout` を出さず、
無出力のまま `return false;` している。

`std::cout` はプロジェクトのログ経路に載らない。`main.cpp` では webrtc::LogMessage
の `LogToDebug` と `FileRotatingLogSink` (`./webrtc_logs` ファイル) がログ経路であり、
`RTC_LOG` はそこに流れる。`std::cout` は stdout に出るだけなので、行番号だけでは
原因究明の役に立たない。メッセージを出すなら AGENTS.md の「ログメッセージは全て英語に
すること」に従い、英語で意味のある内容にすべきである。

## 設計方針

- `std::cout << __LINE__ << std::endl;` を 19 箇所すべて削除し、`RTC_LOG(LS_ERROR)` に置き換える
- `size-max` の 3 箇所 (無出力のエラーパス) にも `RTC_LOG(LS_ERROR)` を追加し、
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

## 解決方法

`src/zakuro.cpp` の `ParseDataChannels` から `std::cout << __LINE__ << std::endl;` を全廃し、
エラーパスすべてで失敗の理由を英語のメッセージとして `RTC_LOG(LS_ERROR)` に出力するようにした。

- `#include <rtc_base/logging.h>` を追加した
- エラーパスは 22 箇所あり、無出力だった `size-max` の 3 箇所にも理由を追加した
- メッセージは `ParseDataChannels: <内容>` の形に統一し、`label is missing` (欠落)、
  `label must be a string` / `interval must be a number` (型)、`interval must be positive` (範囲)、
  `size-min out of range: <値>` (範囲外) のように原因が分かる内容にした
- 呼び出し側 `Zakuro::Run` の `std::cerr` (`failed to parse DataChannels`) は変更していない
- `boost::json::value& dcs = data_channels;` という別名を削除し、引数を直接参照するようにした

`test/test_config_json.py` の `test_data_channels_error_exits_without_crash` を 28 ケースの
パラメータ化テストにし、各ケースで「シグナル終了しない」「終了コード 1」
「`failed to parse DataChannels` が stderr に出る」「理由のメッセージが stderr に出る」
「標準出力が組み立てたコマンドラインの 1 行だけ」を検証する。理由のメッセージは 22 個すべてを
検証している。あわせて `test_valid_data_channels_are_accepted` を追加し、境界値を含む有効な
`data-channels` が受理され、解析後に開始される DataChannel の送信まで進むことを検証する。

検証したこと:

- `python3 run.py build macos_arm64` が成功する
- `uv run pytest -q` が 88 passed / 1 skipped で通る
- `uv run ruff check .` / `uv run ruff format --check .` / `uvx ty@0.0.84 check .` が通る
- `clang-format -style=file` が `src/` の全ファイルで差分を出さない
- 実バイナリで各エラー入力のメッセージを確認し、既定のログレベルでも理由が stderr と
  `./webrtc_logs_0` に出ることを確認した

`CHANGES.md` の `## develop` の `### misc` に `[UPDATE]` のエントリを追加した。
