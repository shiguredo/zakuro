# ZakuroAudioDeviceModule::Init の再入で device_buffer_ が use-after-free になる

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-adm-init-reentry-uaf
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`ZakuroAudioDeviceModule::Init` を再度呼び出したときに `device_buffer_` を無条件に置換しており、
稼働中のオーディオスレッドが破棄済みの `webrtc::AudioDeviceBuffer` を触って UAF を起こす経路を塞ぐ。

## 現状

`src/zakuro_audio_device_module.h` の `ZakuroAudioDeviceModule::Init` は以下を行っている。

- `device_buffer_ = std::make_unique<webrtc::AudioDeviceBuffer>(env_);`
- `initialized_ = true;`
- `adm_` があれば `adm_->Init()` を呼ぶ

`unique_ptr` の代入は旧オブジェクトを即 delete する。`webrtc::AudioDeviceModule` の契約では `Init()` の複数回呼び出しが許容されており、
Sora SDK / libwebrtc 側の再初期化パスから呼ばれた瞬間、`StartAudioThread` が起動中のスレッドが
旧 `device_buffer_->SetRecordedBuffer(...)` を叩いて UAF する。

`RegisterAudioCallback` で保持した callback ポインタも `Init` の再入で `device_buffer_` が差し替わるとロストする。

## 設計方針

以下のいずれか（推奨は前者）で対処する。

- **idempotent 化**: `Init` の先頭で `if (initialized_) return 0;` を返し、二度目以降は何もしない。
  実質的に `Init` は 1 度きりの初期化として扱う
- **安全な差し替え**: `Init` の内部で `StopAudioThread()` を呼び、オーディオスレッドを完全に止めてから
  `device_buffer_` を差し替え、必要なら再登録する

`ZakuroAudioDeviceModule` は Zakuro のカスタム ADM であり、Sora SDK 側から複数回 `Init` されるユースケースは想定していない。
idempotent 化の方が実装コストと安全性のバランスが良い。

## 完了条件

- `Init()` を 2 回連続で呼んでも既存の `device_buffer_` が破棄されないこと（単体テストで検証可能）
- ADM のライフサイクル（`Create → Init → RegisterAudioCallback → StartRecording → StopRecording → Terminate → 破棄`）を
  1000 回以上ループさせても UAF / セグフォが発生しないこと
