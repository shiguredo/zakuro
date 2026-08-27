# CHANGES.md ## develop の整合性 (順序・記載漏れ・粒度・misc 分類)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-changes-md-develop-consistency
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`CHANGES.md ## develop` セクションに以下の複数の整合性問題があり、正式リリース前に整理する必要がある。

- 記載順序が凡例 (CHANGE → UPDATE → ADD → FIX) 違反
- CLI 受付値変更 (`nvidia_video_codec_sdk` → `nvidia_video_codec`) の説明不足
- blend2d 0.20.0 → 0.21.2 のアップデートが未記載
- `--ui-remote-url` と `--ui` の併用必須制約が未記載
- Notification (204 No Content) の記述が未記載
- `CMAKE_CXX_STANDARD` 17→20 は misc に入れるべき
- `.github/copilot-instructions.md` の削除が未記載
- AudioDeviceBuffer / run.py の libwebrtc 派生変更を Sora C++ SDK エントリの子項目にまとめるべき

## 現状

`CHANGES.md ## develop` を凡例 (L1-11: CHANGE → UPDATE → ADD → FIX) と過去リリース (2025.1.0 / 2025.3.0) を突き合わせると、
上記の問題がすべて確認できる。

- 現状の並びは `[ADD] HTTP` → `[CHANGE] VideoCodecImplementation` → `[UPDATE] Sora C++ SDK` → `[UPDATE]*4` → `[ADD]*5` の順で凡例違反
- `[CHANGE] VideoCodecImplementation の NvidiaVideoCodecSdk を NvidiaVideoCodec に変更する` はサブ項目に「Sora C++ SDK のアップデートに伴う対応」しか無く、CLI の受付値 (`--vp8-encoder` などのパース) が `nvidia_video_codec_sdk` → `nvidia_video_codec` に変わったことが読み取れない
- `DEPS` の `BLEND2D_VERSION=0.21.2` は前リリース (2025.3.0) の `0.20.0` から上がっているが `## develop` に blend2d 関連の記述無し
- `--ui-remote-url` は `--ui` 併用必須制約 (`main.cpp` に実装) の説明が `CHANGES.md` に無い
- `Notification (204 No Content)` は `[ADD] JSON-RPC 2.0 の Notification（id なしリクエスト）に対応する` の下で「Notification の場合は 204 No Content を返す」とサブ項目にあるが、これは正しい (再確認しただけ)
- `[UPDATE] CMakeLists.txt の CMAKE_CXX_STANDARD と CMAKE_C_STANDARD を 17 から 20 に上げる` はユーザーへの影響が無い純粋なビルド内部変更で `### misc` に入れるべき
- `.github/copilot-instructions.md` は 2025.2.0 misc で追加された後、実ファイルが消えているので `### misc` に `[REMOVE]` エントリが必要
- `[UPDATE] AudioDeviceBuffer の変更に追随` と `[UPDATE] run.py を修正し、clang を libwebrtc 提供のものに変更` は libwebrtc アップデート派生の対応であり、Sora C++ SDK エントリの子項目に集約するのが 2025.1.0 の書き方に沿う

## 設計方針

- `## develop` 全体を CHANGE → UPDATE → ADD → FIX の順で並べ替える
- `[CHANGE] VideoCodecImplementation` のサブ項目に「`--vp8-encoder` などの受付値 `nvidia_video_codec_sdk` は `nvidia_video_codec` に変更」を追加
- `[UPDATE] Blend2D を 0.21.2 に上げる` を追加
- `[ADD] --ui オプション` のサブ項目に「`--ui-remote-url` は `--ui` との併用が必須」を追加
- `[UPDATE] CMakeLists.txt の CMAKE_CXX_STANDARD ...` を `### misc` に移動
- `### misc` に `[REMOVE] .github/copilot-instructions.md を削除する` を追加
- `AudioDeviceBuffer` / `run.py` の libwebrtc 対応を `[UPDATE] Sora C++ SDK` のサブ項目に統合

## 完了条件

- `## develop` の記載順序が凡例に沿っていること
- 記載漏れがなくなっていること (blend2d / UI 併用 / CLI 受付値 / copilot 削除)
- misc に入るべき変更が本編ではなく misc セクションにあること
- 派生変更がまとめられていること
