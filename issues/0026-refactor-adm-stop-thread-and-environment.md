# ZakuroAudioDeviceModule の StopAudioThread 非スレッドセーフと Environment 二重化を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-adm-stop-thread-and-environment
- Polished: {YYYY-MM-DD}

## 目的

`ZakuroAudioDeviceModule` に残る 2 つの構造的な問題を修正する。

- `StopAudioThread` が並行呼び出しで join 二重や dangling を起こしうる
- 自身のコンストラクタで独自の `webrtc::Environment` を生成しており、Zakuro::Run で worker_thread が作った Environment と別物になる

## 現状

### StopAudioThread の並行呼び出し問題

`src/zakuro_audio_device_module.cpp` の `StopAudioThread` は
`if (audio_thread_)` チェックの後 `audio_thread_stopped_ = true;` → `join` → `reset` → `stopped_ = false;` の順で実行する。
`Terminate` と destructor で二重呼び出しされ、さらに外部から `StopRecording` で呼ばれる可能性もあり、
複数スレッドから同時に呼ばれると `join` が二重呼び出し、または dangling `unique_ptr` へのアクセスになる。

### Environment 二重生成

`src/zakuro.cpp` は worker_thread の `BlockingCall` の中で `auto env = webrtc::CreateEnvironment();` を作り、
`webrtc::CreateAudioDeviceModule(env, ...)` で内蔵 ADM を生成する。
一方で `src/zakuro_audio_device_module.cpp` のコンストラクタが `env_(webrtc::CreateEnvironment())` として
別途もう 1 個の Environment を生成し、`device_buffer_(env_)` で使う。
`Environment` には `TaskQueueFactory` / `Clock` / `FieldTrials` などが紐付いており、
内蔵 ADM と Zakuro ADM で違うものが動くことになる。

## 設計方針

### StopAudioThread の排他制御

- `StopAudioThread` の全体を `std::mutex` で保護する
- または `atomic` フラグと CAS で「一度だけ停止処理を走らせる」実装にする

destructor / Terminate / StopRecording の 3 経路から並行して呼ばれても、
join は必ず 1 度だけになるようにする。

### Environment の共有

- `ZakuroAudioDeviceModule` のコンストラクタで env を引数として受け取り、
  Zakuro::Run 側の worker_thread で作った env を共有する
- あるいは `env_` メンバー自体を持たない設計にし、`device_buffer_` に必要なら都度取得する

Environment を 1 つにまとめることで、TaskQueueFactory / Clock / FieldTrials が内蔵 ADM と Zakuro ADM で一致する。

## 完了条件

- `StopAudioThread` の並行呼び出しで join 二重や dangling が発生しないこと (テストで検証)
- `ZakuroAudioDeviceModule` と内蔵 ADM が同じ `webrtc::Environment` を共有すること
- ThreadSanitizer 有効ビルドで race が検知されないこと
