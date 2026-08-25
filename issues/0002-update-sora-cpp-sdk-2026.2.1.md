# Sora C++ SDK を 2026.2.1 に上げる

- Created: 2026-08-20
- Completed: {YYYY-MM-DD}
- Branch: feature/update-sora-cpp-sdk-2026.2.1
- Polished: 2026-08-26

## 目的

zakuro が利用する Sora C++ SDK を `2026.2.0-canary.19` から正式リリースの `2026.2.1` に更新する。

`2026.2.1` は `2026.2.0` のホットフィックスリリースであり、DataChannel シグナリング利用時の切断で解放済みの WebSocket に対して `Cancel()` を呼び SIGSEGV でクラッシュする問題を修正している。zakuro は `sora::SoraSignaling` を利用しており、`--sora-data-channel-signaling` 指定時は DataChannel シグナリングの切断経路を通るため、この修正の対象となる。

あわせて `2026.2.0` で導入された以下の変更も取り込まれる。

- `WEBRTC_BUILD_VERSION` (libwebrtc) を `m150.7871.3.1` に更新する
- `BOOST_VERSION` (Boost) を `1.92.0` に更新する
- `CMAKE_VERSION` (CMAKE) を `4.4.2` に更新する

## 現状

- `DEPS` の `SORA_CPP_SDK_VERSION` は `2026.2.0-canary.19`
- 依存バージョンは `WEBRTC_BUILD_VERSION=m150.7871.3.0`、`BOOST_VERSION=1.91.0`、`CMAKE_VERSION=4.3.2`
- `CHANGES.md` の develop に「Sora C++ SDK を `2026.2.0-canary.19` に上げる」のエントリが記載済み

## 設計方針

SDK 関連の依存バージョンを sora-cpp-sdk 2026.2.1 の `DEPS` に合わせて、以下の 4 項目を更新する。`CLI11_VERSION` / `BLEND2D_VERSION` / `OPENH264_VERSION` は zakuro 固有の依存のため変更しない。

- `SORA_CPP_SDK_VERSION` を `2026.2.1` に変更する
- `WEBRTC_BUILD_VERSION` を `m150.7871.3.1` に変更する
- `BOOST_VERSION` を `1.92.0` に変更する
- `CMAKE_VERSION` を `4.4.2` に変更する

`BOOST_VERSION` は buildbase.py の `install_boost` が sora-cpp-sdk のリリース資産名 (`boost-{BOOST_VERSION}_sora-cpp-sdk-{SORA_CPP_SDK_VERSION}_{platform}`) からダウンロード URL を組み立てるため、SDK がバンドルする Boost のバージョン (`1.92.0`) と必ず一致させる必要がある。

`WEBRTC_BUILD_VERSION` は zakuro のコードが libwebrtc のヘッダとライブラリへ直接リンクするため、SDK がビルドされた libwebrtc と ABI を合わせる必要があり、SDK の `DEPS` の値 (`m150.7871.3.1`) と一致させる。

`include/sora` 配下の公開ヘッダ差分は `dyn.h` / `renderer/base_renderer.h` / `ssl_verifier.h` のみで、zakuro はこれらを直接利用していない。zakuro が利用する `SoraSignalingConfig` / `SoraClientContext` / `VideoCodecImplementation` のヘッダは変更されていないため、ソースコードの修正は想定しない。ただしビルドと動作で必ず検証すること。

`CHANGES.md` の `## develop` セクションに `[UPDATE]` エントリを追加する。形式は過去の SDK アップデートのエントリに合わせ、変更した各バージョンを列挙する。

追加するエントリのサンプル:

```markdown
- [UPDATE] Sora C++ SDK を `2026.2.1` に上げる
  - WEBRTC_BUILD_VERSION を `m150.7871.3.1` に上げる
  - CMAKE_VERSION を `4.4.2` に上げる
  - BOOST_VERSION を `1.92.0` に上げる
  - @<GitHub ユーザー名>
```

`@<GitHub ユーザー名>` は対応者の GitHub ユーザー名に置き換えること。

### 影響を受ける SDK の変更

`2026.2.0` で導入された変更のうち、zakuro に影響しうるもの。

- TLS 検証の信頼ストアが OS のシステム CA に切り替わる
  - zakuro は `SoraSignalingConfig::ca_cert` を指定する手段を持たないため、接続先はシステム CA に信頼される証明書を使う必要がある。独自 CA を使う Sora サーバーへは、TLS 検証を無効化する `--insecure` 以外で接続できない
- NVIDIA Pascal 世代以前 (sm_50 〜 sm_70) の GPU サポートが廃止される
  - 該当 GPU では NVIDIA ハードウェアエンコーダー / デコーダーが利用できなくなる
- `SoraClientContext` の ABI が変更される (`ConnectionContext::MediaEngineReference` の保持)
  - この変更は 2026.2.0-canary 系の途中で導入済みであり、zakuro が現に利用する `2026.2.0-canary.19` に既に含まれる。プリビルド SDK は同一タグのヘッダとバイナリがリリース資産として提供されるため、zakuro 側の対応は不要

## 完了条件

以下のプラットフォームで `python run.py build <target>` がエラーなく完了すること。

- `macos_arm64`
- `ubuntu-22.04_x86_64`
- `ubuntu-24.04_x86_64`

また、`test/test_zakuro.py` の `test_version` が `DEPS` 更新後の値 (`sora_cpp_sdk` / `libwebrtc` / `boost`) で通ること。Sora への接続と、WebSocket および DataChannel シグナリング (`--sora-data-channel-signaling`) での切断がクラッシュせず正常に動作すること (2026.2.1 の修正の検証)。

### 検証の内訳

`.github/workflows/build.yml` の CI はビルド (`python3 run.py build <target> --package`) のみで、pytest は実行しない。したがって以下の手動検証は CI 完了だけでは担保されず、実 Sora サーバーに接続して実施する。

前提:

- 実 Sora が必須。`test/.env.template` を参考に `TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY` を設定する
- `test_version` の実行: `cd test && uv run pytest test_zakuro.py -k test_version`

手動検証のバリエーション:

| 観点 | 値 | 対象 |
|---|---|---|
| シグナリング | WebSocket (既定) | 接続・切断が正常に動作する |
| シグナリング | DataChannel (`--sora-data-channel-signaling`) | 接続・切断がクラッシュしない (2026.2.1 の修正検証) |
| role | sendrecv / sendonly / recvonly | 全シグナリング種別で正常に動作する |
| vcs | 1 / 2 / 3 | 複数クライアントでの切断の多重度を検証する |
| 切断経路 | `--duration` 指定での `Disconnect()` | 修正対象の切断パスを実際に踏む |

特に DataChannel シグナリング × `--duration` 指定での切断は、2026.2.1 が修正した「解放済み WebSocket への `Cancel()` による SIGSEGV」が発生する経路そのものなので、vcs=2 以上でもクラッシュしないことを必ず確認する。

## 解決方法

未着手