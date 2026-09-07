# FileRotatingLogSink を RemoveLogToStream せずに破棄していて UAF する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-file-rotating-log-sink-uaf
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`webrtc::LogMessage::AddLogToStream` で登録した `FileRotatingLogSink` を
`RemoveLogToStream` で外さないまま unique_ptr を破棄しており、
破棄後のシンクへ書き込む use-after-free 経路を修正する。

libwebrtc は `rtc_base/logging.h` の `AddLogToStream` で
「stream は `RemoveLogToStream` で外されるまで生存しなければならない」と契約している。
現行の `main` はこの契約に反する。

## 現状

`src/main.cpp` の `main` は以下を実行している。

- `std::unique_ptr<webrtc::FileRotatingLogSink> log_sink(new webrtc::FileRotatingLogSink(...));`
- `log_sink->Init()` の成否確認
- `webrtc::LogMessage::AddLogToStream(log_sink.get(), webrtc::LS_INFO);`
- 各種処理を実行
- `main` が return する際、`log_sink` (unique_ptr) が自動で破棄される

`webrtc::LogMessage` は登録された `LogSink*` を静的なグローバル状態として非所有の raw pointer で保持する。
`RemoveLogToStream` を呼ばずに `log_sink` を破棄すると、静的なリストが dangling pointer を持ったままになる。
libwebrtc はプログラム終了時にこの stream リストをクリーンアップしない (デストラクタ順序が不確定なため
明示的にリークさせる設計。`rtc_base/logging.cc` の `streams_` 参照)。

`main` の return 後は静的オブジェクトの destructor が実行されるフェーズに入る。
このフェーズで libwebrtc や Sora C++ SDK のいずれかから `RTC_LOG` が呼ばれると、
`LogMessage` が登録済みのダングリングポインタへ書き込むため UAF になる。

## 設計方針

`AddLogToStream` 後に到達する return パスは次の 3 つ。

- `--ui-remote-url` を `--ui` 無しで指定した場合の return 1
- `--http-host` / `--http-port` の片方だけを指定した場合の return 1
- 正常終了の return 0

`log_sink` の生成前に return するパス (fd 上限不足、config parse 失敗) は対象外。
`log_sink->Init()` 失敗のパスは `log_sink.reset()` が `AddLogToStream` より前に実行されるため、
登録済みシンクの破棄には当たらない (既存の `init` 失敗時の扱いを維持する)。

上記 3 パスから漏れなく `webrtc::LogMessage::RemoveLogToStream(log_sink.get());` を呼ぶ。
`main` の各 return 箇所に明示的に書くのではなく、登録 (Add) と解除 (Remove) を RAII で対にして
自動化するのが望ましい。RAII ガードのデストラクタでは、必ず先に `RemoveLogToStream` を実行してから
シンクを破棄する順序にする (逆順だと解除前に破棄済みシンクへの書き込み経路が残る)。

## 完了条件

- `AddLogToStream` 後の全ての return パスで `RemoveLogToStream` が実行されること (RAII 化推奨)
- Valgrind で zakuro を実行し、プロセス終了時に UAF が検知されないこと
