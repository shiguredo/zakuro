# CHANGES.md ## develop の整合性 (順序・記載漏れ・粒度・misc 分類)

- Created: 2026-08-27
- Completed: 2026-09-30
- Branch: feature/refactor-changes-md-develop-consistency
- Polished: 2026-09-08
- Milestone: 2026.1.0
- Updated: 2026-09-28

## 目的

`CHANGES.md` の `## develop` セクションに以下の整合性問題があり、正式リリース前に整理する必要がある。

- 記載順序が凡例 (CHANGE → UPDATE → ADD → FIX) 違反
- CLI 受付値変更 (`nvidia_video_codec_sdk` → `nvidia_video_codec`) の説明不足
- blend2d 0.20.0 → 0.21.2 のアップデートが未記載
- CMake の C / C++ 標準 17→20 は misc に入れるべき (記載のシンボル名も実コードと不一致)
- `.github/copilot-instructions.md` と `.github/workflows/claude.yml` の削除が未記載
- buildbase.py の更新が未記載
- GitHub Actions の公式アクション更新 (SHA ピン化) が未記載
- AudioDeviceBuffer / run.py の libwebrtc 派生変更を Sora C++ SDK エントリの子項目にまとめるべき

## 現状

`CHANGES.md` の `## develop` を冒頭の凡例 (CHANGE → UPDATE → ADD → FIX) と過去リリース (2025.1.0 / 2025.3.0) を突き合わせると、
上記の問題が確認できる。直近リリースである 2025.3.1 との差分 (`git diff 2025.3.1..develop`) で照合した結果を記す。

- 現状の並びは `[ADD] HTTP サーバー機能を追加する` → `[CHANGE] VideoCodecImplementation の NvidiaVideoCodecSdk を NvidiaVideoCodec に変更する` → `[UPDATE] Sora C++ SDK ...` → `[UPDATE] AudioDeviceBuffer ...` → `[UPDATE] run.py ...` → `[UPDATE] CLI11 ...` → `[UPDATE] CMakeLists.txt ...` → `[ADD] 6 件` の順で凡例違反
- `[CHANGE] VideoCodecImplementation ...` のサブ項目には「Sora C++ SDK のアップデートに伴う対応」しか無く、CLI の受付値が `nvidia_video_codec_sdk` → `nvidia_video_codec` に変わったことが読み取れない。変更コミット 18f2ac6 で `--vp8-encoder` / `--vp9-encoder` / `--av1-encoder` / `--h264-encoder` / `--h265-encoder` の受付値と src/util.cpp のエンコーダー種別マップが変更されており、現行の src/util.cpp のヘルプ文字列も `nvidia_video_codec` のみ
- `DEPS` の `BLEND2D_VERSION=0.21.2` は 2025.3.0 / 2025.3.1 の `0.20.0` から上がっているが (コミット 0f8e283)、`## develop` に blend2d 関連の記述が無い。同コミットでは include が `<blend2d.h>` → `<blend2d/blend2d.h>` に変わっている
- `[ADD] JSON-RPC 2.0 の Notification（id なしリクエスト）に対応する` には「Notification の場合は 204 No Content を返す」と記載済みで、src/json_rpc.cpp / src/http_server.cpp の実装とも一致するため、本 issue では対応不要 (再確認しただけ)
- `[UPDATE] CMakeLists.txt の \`CMAKE_CXX_STANDARD\` と \`CMAKE_C_STANDARD\` を 17 から 20 に上げる` はユーザーへの影響が無い純粋なビルド内部変更で `### misc` に入れるべき。また実際の変更 (コミット 856abc6) は `set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)` であり、エントリの `CMAKE_CXX_STANDARD` / `CMAKE_C_STANDARD` の表記は実コードと不一致
- `.github/copilot-instructions.md` は 2025.2.0 misc で追加されたファイル (2025.3.1 に存在)。`.github/workflows/claude.yml` も 2025.3.0 から 2025.3.1 に存在。いずれも 2026-06-09 のコミット (1c3b969 / 71f9174) で削除済みだが、`### misc` に削除エントリが無い
- `buildbase.py` はコミット 6e09ff0 (2025-09-20) で更新されている (Boost アーカイブの SHA256 検証、Android SDK platform-tools のインストール、blend2d の iOS ビルド引数修正、iOS ビルド用の clang 選択ロジック修正など) が未記載
- `.github/workflows/build.yml` と `.github/actions/download/action.yml` はコミット 301dd5d (2026-05-20) で公式アクション (checkout / upload-artifact / download-artifact) が SHA ピンにして更新されており、2025.3.0 misc の「actions/checkout を v5 にアップデート」と同種の CI 内部変更なのに未記載
- `[UPDATE] AudioDeviceBuffer ...` (コミット e0159dc) と `[UPDATE] run.py ...` (コミット 1d8e3db) は libwebrtc アップデートに伴う対応で、いずれも 2025.3.1 に存在しない未リリースの変更。2025.1.0 / 2025.2.0 ではこうした派生対応は Sora C++ SDK エントリの子項目に書く流儀であり、独立エントリになっているのは粒度として不整合

## 設計方針

- `## develop` 全体を CHANGE → UPDATE → ADD → FIX の順で並べ替える
  - 順序の基準は `CHANGES.md` 冒頭の凡例と過去リリース (2025.1.0 / 2025.3.0) の記載順とする
  - shiguredo-changelog スキルには CHANGE → ADD → UPDATE → FIX とあるが、本リポジトリの凡例と過去リリース (ならびに同系列の sora / sora-cpp-sdk) は CHANGE → UPDATE → ADD → FIX で統一されているため、本 issue では凡例を正とする
- `[CHANGE] VideoCodecImplementation ...` のサブ項目に「`--vp8-encoder` などの受付値 `nvidia_video_codec_sdk` は `nvidia_video_codec` に変更」を追加する
- `[UPDATE] blend2d のバージョンを \`0.21.2\` に上げる` を追加し、サブ項目で include (`<blend2d.h>` → `<blend2d/blend2d.h>`) の変更を記す
- `[UPDATE] CMakeLists.txt の \`CMAKE_CXX_STANDARD\` ...` を `### misc` に移動し、実際の変更 (`set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)`) に合わせてシンボル名を修正する
- `### misc` に以下のエントリを追加する (削除は後方互換のない変更なので `[CHANGE]` を使う。`[REMOVE]` という種別は存在しない)
  - `[CHANGE] .github/copilot-instructions.md を削除する`
  - `[CHANGE] .github/workflows/claude.yml を削除する`
  - `[UPDATE] buildbase.py を更新する` (Boost の SHA256 検証、Android SDK platform-tools のサポート、blend2d の iOS ビルド修正、iOS ビルド用の clang 選択修正など)
  - `[UPDATE] GitHub Actions の公式アクションを SHA ピンで最新版に更新する`
- AudioDeviceBuffer / run.py の libwebrtc 派生エントリを `[UPDATE] Sora C++ SDK ...` の子項目に統合する
  - 2025.1.0 / 2025.2.0 の Sora C++ SDK エントリが派生対応を子項目として記載している流儀に合わせる

## 完了条件

- `## develop` の記載順序が凡例 (CHANGE → UPDATE → ADD → FIX) に沿っていること
- 記載漏れが解消されていること
  - blend2d 0.21.2 へのアップデート
  - `--vp8-encoder` などの受付値 `nvidia_video_codec` への変更
  - `.github/copilot-instructions.md` と `.github/workflows/claude.yml` の削除
  - buildbase.py の更新、GitHub Actions の公式アクション SHA ピン化
- misc に入るべき変更 (CMake の C / C++ 標準の引き上げ、copilot / claude.yml の削除、buildbase.py の更新、CI アクション更新) が本編ではなく misc セクションにあること
- 派生変更 (AudioDeviceBuffer / run.py) が Sora C++ SDK エントリの子項目にまとめられていること
- Notification (204 No Content) はすでに記載済みのため変更しないこと

## 解決方法

`CHANGES.md` の `## develop` を次のように整理した。並べ替えは見出し行と子項目の組を
単位に行い、エントリの本文は変更していない。

- 本編と `### misc` をどちらも凡例 (CHANGE → UPDATE → ADD → FIX) の順に並べ替えた
  - 本編は 29 件で CHANGE → UPDATE → ADD → FIX
  - `### misc` は 19 件で CHANGE → UPDATE → ADD
- `[CHANGE] VideoCodecImplementation ...` の子項目に、`--vp8-encoder` などの受付値
  `nvidia_video_codec_sdk` が `nvidia_video_codec` に変更になったことを追記した
- `[UPDATE] blend2d のバージョンを `0.21.2` に上げる` を追加し、include の変更
  (`<blend2d.h>` → `<blend2d/blend2d.h>`) を子項目に記した
- `[UPDATE] CMakeLists.txt の `CMAKE_CXX_STANDARD` ...` を `### misc` へ移し、
  実際の変更 (`set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)`)
  に合わせてシンボル名を修正した
- `### misc` に次のエントリを追加した
  - `[CHANGE] `.github/copilot-instructions.md` を削除する`
  - `[CHANGE] `.github/workflows/claude.yml` を削除する`
  - `[UPDATE] GitHub Actions の公式アクションを SHA ピンで最新版に更新する`
  - `[UPDATE] buildbase.py を更新する`
- `[UPDATE] AudioDeviceBuffer ...` と `[UPDATE] run.py ...` を独立エントリから
  `[UPDATE] Sora C++ SDK を `2026.2.1` に上げる` の子項目へ統合した
- `[ADD] JSON-RPC 2.0 の Notification（id なしリクエスト）に対応する` は記載済みのため
  変更していない

検証したこと:

- 本編と `### misc` の種別の並びが凡例順であること
- 記載漏れの 6 項目 (blend2d の更新、受付値の変更、copilot-instructions.md と
  claude.yml の削除、buildbase.py の更新、アクションの SHA ピン化) が記載されていること
- CMake の C / C++ 標準、copilot / claude.yml の削除、buildbase.py の更新、
  CI アクションの更新が `### misc` にあること
- 派生変更 2 件が Sora C++ SDK の子項目にあり、独立エントリとして残っていないこと
- 変更前後でエントリの欠落・重複がないこと (変更前の全エントリが変更後のいずれかに
  存在するか、意図した統合・移動であることを機械的に確認した)
- 各エントリの子項目が減っていないこと

issue に記載した事実を実コードで再確認した。

- `src/util.cpp` の `--*-encoder` の受付値は `nvidia_video_codec` のみ
- `DEPS` の `BLEND2D_VERSION` は `0.21.2` で、include は `<blend2d/blend2d.h>`
- `CMakeLists.txt` は `set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)`
- `.github/copilot-instructions.md` と `.github/workflows/claude.yml` は削除済み
- `.github/workflows/*.yml` の公式アクションは SHA ピンで指定されている
- `buildbase.py` の更新には Boost の SHA256 検証、Android SDK の platform-tools、
  iOS ビルド用の clang 選択が含まれる
