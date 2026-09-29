# `--duration` を指定したプロセスの正常終了が SIGABRT になる

- Created: 2026-09-30
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-duration-exit-abort
- Polished: {YYYY-MM-DD}

## 目的

`--duration` を指定した Zakuro は、指定時間の経過後に切断して正常終了する経路を通るが、
終了時に SIGABRT (exit code 134) で落ちる。正常終了のつもりで異常終了するため、
シェルスクリプトや CI から Zakuro を呼ぶ利用者が終了コードで成否を判定できない。

## 現状

実バイナリで再現する。

- 設定ファイルに `duration` を 2 秒、`no-video-device` と `no-audio-device` を true、
  `sora.signaling-url` を接続できない URL にしたインスタンスで起動すると、
  3 回の実行すべてで exit code 134 (Abort trap: 6) になった
- 同じ設定から `duration` だけを削除するとプロセスは終了しない
  (Sora への接続が成立しないため継続して再試行する)。つまり abort は
  `--duration` による終了経路で起きている
- 標準エラー出力の最後は `SoraSignaling::~SoraSignaling` で、abort の直前に
  メッセージは出ない
- `lldb` では SIGABRT の停止位置が `__pthread_kill` までしか取れず、
  LTO 有効ビルドのためかスタックトレースから原因の関数を特定できなかった
- この問題は issues/0034 / issues/0012 / issues/0008 が扱った変更の前から存在し、
  それらの変更に起因するものではない

`src/zakuro.cpp` の `Zakuro::Run` は `duration` が 0 以外の場合に
`ScenarioData::Sleep` → `Disconnect` → (`repeat_interval` が 0 なら) `Exit` の
シナリオを組み立てる。`src/main.cpp` の `main` はインスタンスごとに `std::thread` を
起動して `Zakuro::Run` を呼び、最後に全ての `join` を待ってから stats スレッドを
`join` する。

`test/` に `--duration` を使うテストは無く、この終了経路は自動テストで覆われていない。

## 設計方針

原因を特定してから修正方針を決める。未確定のため、原因の特定を本 issue の最初の作業とする。
候補として次を確認する。

- `main` の `return` 後に残っているスレッドや静的オブジェクトの破棄順
- `InstalledFileLogSink` が `RemoveLogToStream` する前に他のスレッドが `RTC_LOG` を
  呼んでいないか
- `Zakuro::Run` の終了経路で `join` されないスレッドが残っていないか
- `joinable` な `std::thread` の破棄、または libc++ のハードニング検査

## 完了条件

- `--duration` を指定したプロセスが、指定時間の経過後に exit code 0 で終了すること
- 3 回以上連続して実行しても SIGABRT (exit code 134) が出ないこと
- `--duration` を指定しない場合の挙動 (終了しない) が変わらないこと
- 実バイナリを起動する pytest で `--duration` 経路の終了コードを検証すること
  (現状は `test/` に `--duration` を使うテストが無い)

## 解決方法
