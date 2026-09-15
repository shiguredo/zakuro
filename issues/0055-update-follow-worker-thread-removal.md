# Sora C++ SDK の worker_thread 削除に追随する

- Created: 2026-09-15
- Completed: {YYYY-MM-DD}
- Branch: feature/update-follow-worker-thread-removal
- Polished: {YYYY-MM-DD}

## 目的

libwebrtc の issue 558821261「Deprecate and remove PeerConnectionFactoryDependencies::worker_thread」で worker thread が廃止される。CL 501620「Default worker thread to network thread」と CL 502480「Warn when a distinct worker thread is configured」はマージ済みで、削除系の CL 499302 / 501640 / 501720 / 502000 / 502500 / 502860 / 502940 / 502960 はレビュー中である。`PeerConnectionFactoryDependencies::worker_thread` と `PeerConnectionFactoryInterface::worker_thread()` は将来削除される。

対応は 2 段階に分ける。

- 方針 1 (いますぐ実施): 専用の worker thread をやめて network thread を使う
- 方針 2 (558821261 を実装した libwebrtc をマージした後に実施): worker_thread の利用箇所と API を全て無くす

本リポジトリは worker thread を独自生成しておらず、sora-cpp-sdk の `SoraClientContext` が作った `PeerConnectionFactoryDependencies` を `configure_dependencies` コールバックで借りて `dependencies.worker_thread` を参照しているだけである。したがって方針 2 が該当する。方針 1 の時点では sora-cpp-sdk が `dependencies.worker_thread` に network thread を渡すため、本リポジトリは変更不要で、方針 2 まで遅延できる。

## 現状

- `src/zakuro.cpp` の `Zakuro::Run` 内で `context_config.configure_dependencies` に設定するラムダの 2 箇所が `dependencies.worker_thread` を使っている。1 箇所目は `ZakuroAudioDeviceModule::Create` を worker thread 上で実行して ADM を生成する処理、2 箇所目は生成した ADM を `dependencies.adm` に設定する処理である。2 回の `BlockingCall` は 1 回にまとめられる。
- `dependencies` は sora-cpp-sdk の `SoraClientContextConfig::configure_dependencies` 経由で受け取っており、自前で `PeerConnectionFactoryDependencies` を構築していない。独自の worker thread も生成していない。
- `ZakuroAudioDeviceModule` 自身は worker thread に依存しない (`src/zakuro_audio_device_module.cpp`)。
- 依存は `DEPS` の `SORA_CPP_SDK_VERSION=2026.2.0-canary.19` / `WEBRTC_BUILD_VERSION=m150.7871.3.0`。
- 既存の open issue `issues/0026-refactor-adm-stop-thread-and-environment.md` は本文で `dependencies.worker_thread->BlockingCall` を前提として記述しているため、本 issue の対応時に 0026 の記述も実態に合わせて更新する必要がある。

## 設計方針

- 前提条件: sora-cpp-sdk の worker_thread 削除がリリースされ、`SORA_CPP_SDK_VERSION` を更新できる状態になっていること。現時点では存在しないため、本 issue には着手できない。
- `dependencies.worker_thread` を `dependencies.network_thread` に置き換え、2 回の `BlockingCall` を 1 回にまとめる。
- `DEPS` の `SORA_CPP_SDK_VERSION` を更新する。
- `issues/0026-refactor-adm-stop-thread-and-environment.md` の記述を更新する。

## 完了条件

- `worker_thread` の参照が 0 件であること。
- `--no-audio-device` / `--fake-audio-capture` / 自動生成音声で動作確認できていること。
- `CHANGES.md` の `## develop` にエントリが追記されていること。

## 解決方法

(実装時に記入)
