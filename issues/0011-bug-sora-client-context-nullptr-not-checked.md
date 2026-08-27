# sora::SoraClientContext::Create の nullptr 返却を無検査で使用してクラッシュする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-sora-client-context-nullptr-not-checked
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`sora::SoraClientContext::Create` は環境依存の複数経路で nullptr を返すが、
`Zakuro::Run` はこれを無検査で `VirtualClient::Connect` に渡し、
`config_.context->peer_connection_factory()` で nullptr dereference によりクラッシュする経路を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` は `vc_config.context = sora::SoraClientContext::Create(context_config);` を実行し、
`vc_config.context` が nullptr かどうかを検査せずに `vc_configs` へ流し込む。

Sora C++ SDK 側 (`sora::SoraClientContext::Create`) は PeerConnectionFactory の生成失敗、
signaling / worker / network thread の起動失敗、encoder factory の生成失敗など複数箇所で `nullptr` を返す。

`src/virtual_client.cpp` の `VirtualClient::Connect` は無条件に
`config_.context->peer_connection_factory()->CreateAudioTrack(...)` を叩き、nullptr dereference で SIGSEGV する。

GPU コンテキスト初期化失敗、OpenH264 DLL の open 失敗、AudioLayer 生成失敗など、
実行環境固有の理由で頻発しうる。

## 設計方針

`Zakuro::Run` の以下の場所で nullptr を検査し、エラーメッセージを出して `return 1;` する。

- `vc_config.context = sora::SoraClientContext::Create(context_config);` の直後で nullptr チェック
- エラーログは `RTC_LOG(LS_ERROR) << ...;` で英語で出力（AGENTS.md の規約に従う）

`VirtualClient::Create` 側でも defensive に `assert(config.context)` を入れておくと、
テストや将来の別呼び出しからのミスにも気付ける。

## 完了条件

- `SoraClientContext::Create` が nullptr を返すシナリオ（例: 存在しない OpenH264 パスを `--openh264` で指定）で
  クラッシュせず、明確なエラーメッセージを出して非ゼロ終了すること
- 正常経路には影響しないこと
