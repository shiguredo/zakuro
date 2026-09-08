# ZakuroAudioDeviceModule の StopAudioThread 非スレッドセーフと Environment 二重化を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-adm-stop-thread-and-environment
- Polished: 2026-09-08

## 目的

`ZakuroAudioDeviceModule` に残る 2 つの構造的な問題を修正する。

- `StopAudioThread` が並行呼び出しで join 二重や dangling を起こしうる
- 自身のコンストラクタで独自の `webrtc::Environment` を生成しており、Zakuro::Run 側で生成した Environment と別物になる

## 現状

### StopAudioThread の並行呼び出し問題

`src/zakuro_audio_device_module.cpp` の `StopAudioThread` は
`if (audio_thread_)` チェックの後 `audio_thread_stopped_ = true;` → `join` → `reset` → `audio_thread_stopped_ = false;` の順で実行する。
`Terminate` と destructor で二重呼び出しされ、さらに外部から `StopRecording` で呼ばれる可能性もあり、
複数スレッドから同時に呼ばれると `join` が二重呼び出し、または dangling `unique_ptr` へのアクセスになる。

なお、`Terminate` 内の解放順序 (`device_buffer_` を先に破棄する問題) は issues/0003、
`Init` の再入による `device_buffer_` の置き換えは issues/0004 がそれぞれ別途対応する。
本 issue で扱うのは `StopAudioThread` の並行呼び出し安全性と Environment の共有の 2 点のみである。

### Environment 二重生成

`src/zakuro.cpp` の configure_dependencies コールバック内
(`SoraClientContext::Create` が実行する `dependencies.worker_thread->BlockingCall`) で
`auto env = webrtc::CreateEnvironment();` を作り、Type::ADM のときは
`webrtc::CreateAudioDeviceModule(env, ...)` で内蔵 ADM を生成する。
一方で `src/zakuro_audio_device_module.cpp` のコンストラクタが `env_(webrtc::CreateEnvironment())` として
別途もう 1 個の Environment を生成し、`Init()` の
`device_buffer_ = std::make_unique<webrtc::AudioDeviceBuffer>(env_)` で使う。
`Environment` には `TaskQueueFactory` / `Clock` / `FieldTrials` / `RtcEventLog` が紐付いており、
内蔵 ADM と Zakuro ADM で違うものが動くことになる。

なお、Sora C++ SDK 側でも `SoraClientContext::Create` と `PeerConnectionFactoryWithContext` が
PeerConnectionFactory / ConnectionContext 用に別の `webrtc::CreateEnvironment()` を生成している。
本 issue の対象は ZakuroAudioDeviceModule と内蔵 ADM の間の共有に限定する。

## 設計方針

### StopAudioThread の排他制御

`audio_thread_` の読み取り・代入・破棄を行う箇所 (`StopAudioThread` の全体と `StartAudioThread` の代入) を
1 つの `std::mutex` で保護する。`join` は 10 ミリ秒周期でループするスレッドの停止を待つだけなので、
ミューテックスを保持したまま `join` しても短時間で済む。
または atomic な状態フラグと CAS で「停止処理を一度だけ走らせる」実装にする。
この場合は `StartAudioThread` で停止状態を解除 (再武装) することを忘れないこと。
再武装しないと 2 回目以降の `StartRecording` 後の `StopRecording` で停止処理が走らず、スレッドが残り続ける。

destructor / Terminate / StopRecording の 3 経路から並行して呼ばれても、
join は必ず 1 度だけになるようにする。

### Environment の共有

- 案 1: `ZakuroAudioDeviceModule` のコンストラクタで `webrtc::Environment` を引数として受け取り、
  `env_` に保存して `Init()` の `device_buffer_` 生成に使う
- 案 2: `ZakuroAudioDeviceModuleConfig` に `webrtc::Environment` メンバーを持たせ、
  `Init()` では `config_` の env を使って `device_buffer_` を生成する (専用の `env_` メンバーは持たない)。
  `webrtc::Environment` はデフォルト構築できないため、sora-cpp-sdk の `sora::AudioDeviceModuleConfig` と同様に
  `webrtc::Environment env = webrtc::CreateEnvironment();` の形で初期化しておくこと

どちらの案でも、Zakuro::Run 側のコールバック内で生成した env を
`webrtc::CreateAudioDeviceModule` の引数と `ZakuroAudioDeviceModule::Create` の両方に渡す。
これにより Environment は 1 つになり、TaskQueueFactory / Clock / FieldTrials が内蔵 ADM と Zakuro ADM で一致する。

## 完了条件

- `StopAudioThread` の並行呼び出しで join 二重や dangling が発生しないこと
  - 複数スレッドから `StopAudioThread` を同時に呼び出す C++ 単体テストで検証する。
    C++ 単体テスト基盤は未整備のため、issues/0043 で導入予定の GoogleTest / doctest / Catch2 のいずれかを
    使ったテストターゲットの追加が必要
- Zakuro::Run 側で生成した 1 つの `webrtc::Environment` を、
  `ZakuroAudioDeviceModule` の `device_buffer_` 生成と内蔵 ADM の生成 (Type::ADM のとき) の両方に使うこと
- 可能であれば ThreadSanitizer 有効ビルドで、`StopAudioThread` まわりの race が検知されないこと
