# Sora C++ SDK を 2026.2.1 に上げる

- Created: 2026-08-20
- Completed: {YYYY-MM-DD}
- Branch: feature/update-sora-cpp-sdk-2026.2.1
- Polished:

## 目的

zakuro が利用する Sora C++ SDK を `2026.2.0-canary.19` から正式リリースの `2026.2.1` に更新する。

`2026.2.1` は `2026.2.0` のホットフィックスリリースであり、DataChannel シグナリング利用時の切断で解放済みの WebSocket に対して `Cancel()` を呼び SIGSEGV でクラッシュする問題を修正している。zakuro は `sora::SoraSignaling` を利用するため、この修正の恩恵を受ける。

あわせて `2026.2.0` で導入された以下の変更も取り込まれる。

- libwebrtc を `m150.7871.3.1` に更新する
- Boost を `1.92.0` に更新する
- CMAKE を `4.4.2` に更新する

## 現状

- `DEPS` の `SORA_CPP_SDK_VERSION` は `2026.2.0-canary.19`
- 依存バージョンは `WEBRTC_BUILD_VERSION=m150.7871.3.0`、`BOOST_VERSION=1.91.0`、`CMAKE_VERSION=4.3.2`
- `CHANGES.md` の develop に「Sora C++ SDK を `2026.2.0-canary.19` に上げる」のエントリが記載済み

## 設計方針

`DEPS` を sora-cpp-sdk 2026.2.1 の `DEPS` に合わせて更新する。

- `SORA_CPP_SDK_VERSION` を `2026.2.1` に変更する
- `WEBRTC_BUILD_VERSION` を `m150.7871.3.1` に変更する
- `BOOST_VERSION` を `1.92.0` に変更する
- `CMAKE_VERSION` を `4.4.2` に変更する

`BOOST_VERSION` は buildbase.py の `install_boost` が sora-cpp-sdk のリリース資産名 (`boost-{BOOST_VERSION}_sora-cpp-sdk-{SORA_CPP_SDK_VERSION}_{platform}`) からダウンロード URL を組み立てるため、SDK がバンドルする Boost のバージョン (`1.92.0`) と必ず一致させる必要がある。

`include/sora` 配下の公開ヘッダ差分は `dyn.h` / `renderer/base_renderer.h` / `ssl_verifier.h` のみで、zakuro はこれらを直接利用していない。zakuro が利用する `SoraSignalingConfig` / `SoraClientContext` / `VideoCodecImplementation` のヘッダは変更されていないため、ソースコードの修正は想定しない。ただしビルドと動作で必ず検証すること。

`CHANGES.md` の `## develop` セクションに `[UPDATE]` エントリを追加する。形式は過去の SDK アップデートのエントリに合わせ、変更した各バージョンを列挙する。

追加するエントリのサンプル:

```markdown
- [UPDATE] Sora C++ SDK を `2026.2.1` に上げる
  - WEBRTC_BUILD_VERSION を `m150.7871.3.1` に上げる
  - CMAKE_VERSION を `4.4.2` に上げる
  - BOOST_VERSION を `1.92.0` に上げる
  - <GitHub ユーザー名>
```

`@<GitHub ユーザー名>` は対応者名に置き換えること。対応者名が確定するまで `CHANGES.md` の追記コミットは行わないこと。

### 影響を受ける SDK の変更

`2026.2.0` で導入された変更のうち、zakuro に影響しうるもの。

- TLS 検証の信頼ストアが OS のシステム CA に切り替わる
  - zakuro は `SoraSignalingConfig::ca_cert` を指定していないため、接続先はシステム CA に信頼される証明書を使う必要がある
- NVIDIA Pascal 世代以前 (sm_75 未満) の GPU サポートが廃止される
  - 該当 GPU では NVIDIA ハードウェアエンコーダー / デコーダーが利用できなくなる
- `SoraClientContext` の ABI が変更される (`ConnectionContext::MediaEngineReference` の保持)
  - プリビルド SDK をヘッダごと再ビルドするため、zakuro 側の対応は不要

## 完了条件

以下のプラットフォームで `python run.py build <target>` がエラーなく完了すること。

- `macos_arm64`
- `ubuntu-22.04_x86_64`
- `ubuntu-24.04_x86_64`

また、`test/test_zakuro.py` の `test_version` が `DEPS` 更新後の値 (`sora_cpp_sdk` / `libwebrtc` / `boost`) で通ること。Sora への接続と、WebSocket および DataChannel シグナリングでの切断がクラッシュせず正常に動作すること (2026.2.1 の修正の検証)。

## 解決方法

未着手