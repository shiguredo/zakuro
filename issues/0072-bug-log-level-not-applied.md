# `--log-level` が stderr のログ出力を絞り込めていない

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-log-level-not-applied
- Polished: {YYYY-MM-DD}

## 目的

`--log-level` に `none` を指定しても stderr に `LS_INFO` / `LS_ERROR` のログが出続けており、
利用者が指定したログレベルでログの量を制御できない。負荷試験では 1 インスタンスあたり
大量のログが出るため、`--log-level none` が効かないことはログ量の見積もりとディスク消費に
直結する。

## 現状

`src/main.cpp` の `main` は、`--log-level` で受け取った値を `LogMessage::LogToDebug` に
渡している。既定値は `webrtc::LS_NONE` である。

一方でログファイル用の `InstalledFileLogSink` は、`FileRotatingLogSink` を
`LogMessage::AddLogToStream(sink, webrtc::LS_INFO)` で登録している。このシンクは
`(webrtc::LoggingSeverity)log_level` では制御されないため、`LS_INFO` 以上のログは
`--log-level` の値に関わらず stderr に出続ける。

確認したこと (実バイナリで実測):

- `--log-level` を `none` / `info` / `warning` / `error` / `verbose` の 5 通りで実行しても、
  stderr の出力行は件数・内容とも同一である (正常設定を 6 秒動かして 75 行、差分は
  タイムスタンプのみ)
- `--log-level none` を指定しても、既定値である `src/main.cpp` の
  `file descriptor limit: ...` の `LS_INFO` ログが stderr に出る
- `--log-level bogus` は CLI11 が 105 で弾くため、オプション自体は解析されている

`README.md` の `--log-level` の説明は `verbose` / `info` / `warning` / `error` / `none` を
受け付ける旨のみで、実際にはどの値でも同じ出力になることは書かれていない。

なお issue 0032 (`AGENTS.md` 規約違反のログメッセージ言語を修正する) の設計方針には
「`RTC_LOG(LS_ERROR)` へ移すと既定のログレベル (`LS_NONE`) では stderr に出力されず
ログファイルにしか残らない」という前提があるが、この前提は現行実装では成立していない。
本 issue を解決すると前提が成立する方向に変わるため、0032 の設計方針も本 issue の結果に
合わせて見直す必要がある。

## 設計方針

- WebRTC のログレベル制御のうち、シンク側に固定されている `LS_INFO` を `--log-level` の値で
  制御できるようにする。`LogMessage` の現在の API (`LogToDebug` / `AddLogToStream`) で
  実現できない場合は、libwebrtc が提供する新しいログ初期化 API の利用を検討する
- `--log-level none` を指定したときに stderr にログが出ないこと、`--log-level error` を
  指定したときに `LS_ERROR` 以上だけが出ることを仕様とする
- `--log-level` はログファイル (`./webrtc_logs_0`) の出力にも同じ基準で適用する
- 既定値 (`LS_NONE`) のときに何も出なくなるため、既存テストがログを同期点にしている場合は
  テスト側の指定も合わせて見直す

## 完了条件

- `--log-level none` を指定した場合、stderr にログが出力されないこと
- `--log-level error` を指定した場合、`LS_ERROR` 以上のログだけが stderr に出力されること
- `--log-level verbose` を指定した場合、`LS_VERBOSE` 以上のログが出力されること
- `--log-level` を指定しない場合の挙動 (既定値) が仕様として説明できること
- 既存の pytest がすべて pass すること (ログ出力を前提にしているテストは、ログレベルを
  明示する形に更新する)
- `python3 run.py build macos_arm64` などのビルドが通ること

## 解決方法

{YYYY-MM-DD} に記入
