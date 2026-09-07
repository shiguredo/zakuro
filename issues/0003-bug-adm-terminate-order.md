# ZakuroAudioDeviceModule::Terminate の解放順序でセグフォする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-adm-terminate-order
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`ZakuroAudioDeviceModule::Terminate` がオーディオスレッドの停止より先に `device_buffer_` を破棄しており、
停止までの間にオーディオスレッドが破棄済みバッファを触って SIGSEGV する経路を修正する。

デストラクタからも `Terminate` が呼ばれるため、プロセス正常終了時にも高確率で発生する。

## 現状

`src/zakuro_audio_device_module.h` の `ZakuroAudioDeviceModule::Terminate` は以下の順序で処理している。

1. 状態フラグを false にリセット
2. `device_buffer_.reset()` で `webrtc::AudioDeviceBuffer` を破棄
3. `StopAudioThread()` でオーディオスレッドを停止 (join)

一方 `src/zakuro_audio_device_module.cpp` の `StartAudioThread` が起動するスレッドは、
`while (!audio_thread_stopped_)` のループ中で 10 ミリ秒ごとに `device_buffer_->SetRecordedBuffer(...)` と
`device_buffer_->DeliverRecordedData()` を呼び続ける。

`device_buffer_.reset()` が実行された直後で `StopAudioThread()` の join 完了前に、
オーディオスレッドが `unique_ptr::operator->()` に対して nullptr dereference を発生させる。

## 設計方針

`Terminate` の解放順序を反転させる。

1. 状態フラグを false にリセット
2. `StopAudioThread()` を呼び、オーディオスレッドを完全に join する
3. `device_buffer_.reset()` で `webrtc::AudioDeviceBuffer` を破棄

`device_buffer_` は atomic ではないため、スレッドを完全に停止してから破棄する以外に安全な順序はない。
`adm_` (内蔵 ADM) の `Terminate()` はどちらの順序でも問題ないが、
スレッド停止後・buffer 破棄後に呼ぶ順序に統一する。

## 完了条件

- `Terminate()` の中で `StopAudioThread()` が `device_buffer_.reset()` より先に呼ばれていること
- macOS arm64 / Ubuntu 22.04 / Ubuntu 24.04 で `--vcs 2 --duration 5 --repeat-interval 1` を数分間走らせ、
  終了時にセグフォが発生しないこと（`--repeat-interval 1` のためプロセスは自然終了しないので、
  SIGINT / SIGTERM で終了させる）
- 可能であれば ThreadSanitizer / AddressSanitizer 有効ビルドで、
  `webrtc::AudioDeviceBuffer` へのアクセスが race や UAF として検知されないこと
