# README のバッジ・ヘルプが実装と乖離している (libwebrtc / --port / 動作環境)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-readme-libwebrtc-badge-and-port-option
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`README.md` の記述が develop の実装と複数箇所で乖離しており、
利用者が正しくない情報を元にビルド・利用を試みる状況になっている。
以下 3 点を最新の実装に合わせて修正する。

## 現状

### libwebrtc バッジのバージョンが古い

`README.md` の冒頭にある libwebrtc バッジは `libwebrtc-m141.7390`。
`DEPS` の `WEBRTC_BUILD_VERSION` は `m150.7871.3.0`、`CHANGES.md ## develop` にも
「WEBRTC_BUILD_VERSION を `m150.7871.3.0` に上げる」と明記されている。

### 削除された `--port` オプションがヘルプ抜粋に残存

`README.md` の埋め込みヘルプ抜粋には `--port INT:INT in [-1 - 65535]` が載っているが、
`src/util.cpp` の `Util::ParseArgs` には `--port` の `add_option` は存在しない。
実装から消えているのに README ヘルプにだけ残っている。

### 動作環境と CMakeLists.txt の整合性欠落

`README.md` の動作環境は `Ubuntu 22.04 x86_64` / `Ubuntu 24.04 x86_64` / `macOS 15 arm64`。
一方 `CMakeLists.txt` の `ZAKURO_PLATFORM` 判定には `ubuntu-20.04_x86_64` 分岐が残っており、
`CHANGES.md 2025.1.0` で「Ubuntu 20.04 のビルドを削除」と明記されているのに削除されていない。

## 設計方針

- README バッジの libwebrtc バージョンを `m150.7871` (または `m150.7871.3`) に更新する
- README ヘルプ抜粋から `--port` の 2 行を削除する
- README 全体のヘルプ抜粋を、実装 (`src/util.cpp`) に合わせて再生成することが望ましい
  (別 issue で `--http-host` / `--http-port` / `--ui` / `--ui-remote-url` 追加を扱う。本 issue は既存の乖離除去だけに絞る)

## 完了条件

- `README.md` の libwebrtc バッジが DEPS の `WEBRTC_BUILD_VERSION` と一致していること
- `README.md` ヘルプ抜粋に `--port` オプションが記載されていないこと
- 動作環境の記述と `CMakeLists.txt` の分岐の整合性が取れていること (別 issue の `feature/remove-ubuntu-2004-branch` と連動)
