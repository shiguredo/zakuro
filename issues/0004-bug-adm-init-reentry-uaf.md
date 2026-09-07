# ZakuroAudioDeviceModule::Init の再入で device_buffer_ が use-after-free になる

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-adm-init-reentry-uaf
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`ZakuroAudioDeviceModule::Init` を再入安全にする。現状の実装は `Init` のたびに
`device_buffer_` を無条件に `make_unique` で置き換えるため、オーディオスレッド
（`StartAudioThread` が起動するスレッド）が稼働中に `Init` が再入すると、破棄済みの
`webrtc::AudioDeviceBuffer` をオーディオスレッドが触って UAF する経路が残っている。

## 現状

`src/zakuro_audio_device_module.h` の `ZakuroAudioDeviceModule::Init` は以下を行っている。

- `device_buffer_ = std::make_unique<webrtc::AudioDeviceBuffer>(env_);`
- `initialized_ = true;`
- `adm_` があれば `adm_->Init()` を呼ぶ

`unique_ptr` の代入は旧オブジェクトを即 delete する。
`device_buffer_` は `src/zakuro_audio_device_module.cpp` の `StartAudioThread` が起動する
スレッドが、ループ中に `device_buffer_->SetRecordedBuffer(...)` と
`device_buffer_->DeliverRecordedData()` でアクセスする。

`webrtc::AudioDeviceModule` の参照実装（`webrtc::AudioDeviceModuleImpl::Init`
`modules/audio_device/audio_device_impl.cc`）は `if (initialized_) return 0;` として
多重呼び出しを no-op にする idempotent な実装である。`Init` の複数回呼び出しは
想定された使い方であり、`Init` と `Terminate` を繰り返すことが許容されている。
この Zakuro の実装はここに反している。

実際の再入経路は VirtualClient の再接続である。`src/virtual_client.cpp` の
`OnDisconnect` → `Connect` が SoraSignaling の PeerConnection を作り直すと、
libwebrtc の `ConnectionContext::AddRefMediaEngine`（`MediaEngineReference`）が
`media_engine_->Init()` を呼び、`WebRtcVoiceEngine::Init` → `adm_helpers::Init` →
`adm->Init()` の順で `ZakuroAudioDeviceModule::Init` が再度呼ばれる。

この再入は切断側の `ReleaseMediaEngine` → `adm()->Terminate()` がスレッド停止を
完了した後に直列に走るため、現行フローでは通常 UAF に至らない。しかし
`StartAudioThread` の稼働中に `Init` を呼ぶことを防ぐ仕組みは今の実装にはなく、
その場合に旧 `device_buffer_` へのアクセスで UAF する。また `RegisterAudioCallback`
で登録した callback ポインタは旧バッファに紐づくため、`Init` 再入で置き換えられると
失われる（その後 `RegisterAudioCallback` が再登録されない限り音声が配信されない）。

## 設計方針

`Init` を idempotent にする。`Init` の先頭で `if (initialized_) return 0;` とし、
二度目以降は何もしない。

- `Terminate()` が `initialized_ = false` にするため、Terminate 後の再初期化
  （再接続時）では従来通り `device_buffer_` を生成し直す
- 再入時に `device_buffer_` を破棄・置換しないため、オーディオスレッド稼働中でも安全
- `webrtc::AudioDeviceModule` の参照実装と同じ挙動になり、callback 登録も維持される

「`StopAudioThread()` で完全に停止してから `device_buffer_` を差し替え、必要なら
`RegisterAudioCallback` を再登録する」案もあるが、録音中に `Init` が呼ばれると録音が
中断される上に再登録処理が必要になる。これに対し idempotent 化は録音を止めずに
再入を無害化でき、参照実装とも一致するため採用しない。

## 完了条件

- `Init()` を 2 回連続で呼んでも既存の `device_buffer_` が破棄されないこと
  （2 回目の `Init()` が 0 を返し、初回 `Init` 後に `RegisterAudioCallback` で登録した
  callback が 2 回目の `Init` 後にも呼び出されることを C++ の単体テストで検証する。
  C++ 単体テスト基盤は未整備のため、テストターゲットの追加が必要。issues/0043 では
  GoogleTest / doctest / Catch2 のいずれかの導入が予定されている）
- ADM のライフサイクル（`Create → Init → Init（2 回目）→ RegisterAudioCallback →
  StartRecording → StopRecording → Terminate → 破棄`）を 1000 回以上ループさせても
  UAF / セグフォが発生しないこと
  （このループは `Terminate` とデストラクタの解放順序も通るため、
  issues/0003 の修正が反映されていることを前提とする）
- 可能であれば AddressSanitizer / ThreadSanitizer 有効ビルドで、`webrtc::AudioDeviceBuffer`
  へのアクセスが race や UAF として検知されないこと
