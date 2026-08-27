# doc/USE.md に develop の HTTP / UI / RPC 機能と依存記述の更新を反映する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/add-use-md-http-ui-rpc-and-deps-update
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`doc/USE.md` に develop の新機能 (`--http-host` / `--http-port` / `--ui` / `--ui-remote-url`) が
1 行も記載されていない。加えて、削除済みのランタイム依存や古い OpenH264 バージョンなど、
実装と乖離した記述が複数残っている。利用者が USE.md だけを見て正しくセットアップ・利用できる状態に戻す。

## 現状

### 新機能記述の欠落

`doc/USE.md` に以下が 1 行も記載されていない。

- `--http-host` / `--http-port` オプションの説明
- `--ui` / `--ui-remote-url` オプションの説明
  - `main.cpp` に「`--ui` 併用必須」制約があるが、それも未記載
- JSONC 設定例 (`doc/USE.md` 内) に `http-port` / `http-host` / `ui` / `ui-remote-url` の記述無し
- `/.ok` エンドポイントの説明無し
- JSON-RPC (`/rpc`) と `GetVersion` メソッドは `doc/RPC.md` にはあるが USE.md からのリンク無し

また USE.md の JSONC 例に `"port": -1` が残っているが、`--port` オプションは実装から消えている。

### ランタイム依存の古い記述

`doc/USE.md` の apt install コマンドに `libdrm2 libva2 libva-drm2` が残っている。
`CHANGES.md 2025.1.0` で `CMakeLists の依存から libva と libdrm を削除` としており、
CI (`.github/workflows/build.yml`) でもインストールしていない。
実行時に本当に必要かを検証し、不要なものは削除する。

### OpenH264 バージョンが古い

`doc/USE.md` の OpenH264 ダウンロード例が `v2.1.1` を参照。
`DEPS` は `OPENH264_VERSION=v2.6.0` で、5 年近く前のバージョンを案内している状態。

## 設計方針

- HTTP / UI 系オプションの説明を新規節として追加する。
  `--ui` 併用必須の制約、デフォルトの `zakuro-ui.shiguredo.app` の挙動を明記する
- JSONC 設定例の `port` を削除、代わりに `http-host` / `http-port` / `ui` / `ui-remote-url` を追加
- `/.ok` エンドポイントの説明を追加、`doc/RPC.md` へのリンクを追加
- 「実行時に必要なランタイム依存」を実測して確認し、不要な `libdrm2` / `libva2` / `libva-drm2` を削除
- OpenH264 のダウンロード URL とファイル名を `v2.6.0` に更新

## 完了条件

- `doc/USE.md` の HTTP / UI / RPC の各機能について、実装と一致した説明があること
- JSONC 設定例に `port` の記述が無く、develop の新オプションが記載されていること
- ランタイム依存の記述が実装と一致していること（実行して検証する）
- OpenH264 バージョンが `DEPS` の値と一致していること
