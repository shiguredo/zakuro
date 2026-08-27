# FileRotatingLogSink を RemoveLogToStream せずに破棄していて UAF する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-file-rotating-log-sink-uaf
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`webrtc::LogMessage::AddLogToStream` で登録した `FileRotatingLogSink` を
`RemoveLogToStream` で外さないまま unique_ptr を破棄しており、
libwebrtc / Sora の静的破棄フェーズでログを吐かれた際に破棄済みシンクへ書き込む use-after-free 経路を修正する。

## 現状

`src/main.cpp` の `main` は以下を実行している。

- `std::unique_ptr<webrtc::FileRotatingLogSink> log_sink(new webrtc::FileRotatingLogSink(...));`
- `log_sink->Init()` の成否確認
- `webrtc::LogMessage::AddLogToStream(log_sink.get(), webrtc::LS_INFO);`
- 各種処理を実行
- `main` が return する際、`log_sink` (unique_ptr) が自動で破棄される

`webrtc::LogMessage` は登録された `LogSink*` を静的なグローバル状態として非所有の raw pointer で保持する。
`RemoveLogToStream` を呼ばずに `log_sink` を破棄すると、静的なリストが dangling pointer を持ったままになる。

`main` の return 後、libwebrtc や Sora C++ SDK の静的オブジェクトの destructor から `RTC_LOG` が呼ばれる可能性があり
(libwebrtc は静的 flush パスや Environment のクリーンアップで実際に発火する)、
このタイミングで破棄済みシンクへの書き込みが UAF になる。

## 設計方針

`main` の末尾 (`log_sink` の unique_ptr が破棄される前) で
`webrtc::LogMessage::RemoveLogToStream(log_sink.get());` を明示的に呼ぶ。

`return 0;` の直前で呼ぶのが最も自然。エラーで途中 return するパス (fd 上限不足、config parse 失敗など) からも
呼ばれるように、RAII で自動化するのが望ましい。

## 完了条件

- `main` からの全ての return パスで `RemoveLogToStream` が実行されること (RAII 化推奨)
- Valgrind / AddressSanitizer 有効ビルドでプロセス終了時に libwebrtc / Sora 側からの
  `RTC_LOG` 呼び出しが起きても UAF が検知されないこと
