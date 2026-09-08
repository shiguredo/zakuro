# README のバッジ・ヘルプが実装と乖離している (libwebrtc / --port)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-readme-libwebrtc-badge-and-port-option
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`README.md` の記述が develop の実装と複数箇所で乖離しており、
利用者が正しくない情報を元にビルド・利用を試みる状況になっている。
以下 2 点を最新の実装に合わせて修正する。

なお、README の動作環境 (macOS 15 arm64 / Ubuntu 22.04 x86_64 / Ubuntu 24.04 x86_64) は
CI (`.github/workflows/build.yml`) のビルド対象と既に一致している。
`CMakeLists.txt` に残る Ubuntu 20.04 x86_64 分岐は README ではなく `CMakeLists.txt` の
修正になるため、issues/0046 で対応する。

## 現状

### libwebrtc バッジのバージョンが古い

`README.md` の冒頭にある libwebrtc バッジは `libwebrtc-m141.7390`。
`DEPS` の `WEBRTC_BUILD_VERSION` は `m150.7871.3.0`、`CHANGES.md ## develop` にも
「WEBRTC_BUILD_VERSION を `m150.7871.3.0` に上げる」と明記されている。

### 削除された `--port` オプションがヘルプ抜粋に残存

`README.md` の埋め込みヘルプ抜粋には `--port INT:INT in [-1 - 65535]` が載っているが、
`src/util.cpp` の `Util::ParseArgs` には `--port` の `add_option` は存在しない。
実装から消えているのに README ヘルプにだけ残っている。

## 設計方針

- README バッジの libwebrtc バージョンを `m150.7871` に更新し、リンク先を
  `branch-heads/7871` に変更する
  - 既存バッジは DEPS の `WEBRTC_BUILD_VERSION` から `m<マイルストーン>.<branch-head>` を
    抜き出した形式 (例: DEPS `m141.7390.2.0` に対して `m141.7390`)。DEPS は
    `m150.7871.3.0` なので `m150.7871` になる
  - パッチ部 (`3.0`) は含めない。issue 0002 で `m150.7871.3.1` への更新も予定されており、
    マイルストーンと branch-head のみなら更新の影響を受けない
- README ヘルプ抜粋から `--port` の 2 行を削除する
- README 全体のヘルプ抜粋を、実装 (`src/util.cpp`) に合わせて再生成することが望ましい
  (再生成と `--http-host` / `--http-port` / `--ui` / `--ui-remote-url` の追加は issues/0018 で扱う。
  本 issue は既存の乖離除去だけに絞る)
  - issues/0018 と本 issue は README の同一のヘルプ抜粋ブロックを触るため、
    issues/0018 が先に完了している場合は `--port` の 2 行が既に削除されていることがある

## 完了条件

- `README.md` の libwebrtc バッジが `m150.7871` かつリンク先が `branch-heads/7871` であること
  (DEPS の `WEBRTC_BUILD_VERSION` (`m150.7871.3.0`) のマイルストーンと branch-head に一致)
- `README.md` ヘルプ抜粋に `--port` オプションが記載されていないこと
