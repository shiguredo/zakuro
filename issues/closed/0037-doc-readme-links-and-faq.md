# README から RPC.md / UI.md / SUPPORT.md へのリンク追加と FAQ の壊れたリンクを修正する

- Created: 2026-08-27
- Completed: 2026-09-30
- Branch: feature/fix-readme-links-and-faq
- Polished: 2026-09-08
- Updated: 2026-09-28

## 目的

`README.md` から `doc/RPC.md` / `doc/SUPPORT.md` へのリンクが 1 個もなく、
develop で追加した JSON-RPC のドキュメント (`doc/RPC.md`) と `doc/SUPPORT.md` が
README から参照されない「宙に浮いた」状態になっている。
`doc/FAQ.md` にはリンクテキストが実装と食い違うリンクが残っている。ドキュメントの導線を修正する。

## 現状

### README のリンク欠落

`README.md` からは `doc/USE.md` / `doc/BUILD.md` / `doc/FAQ.md` しか参照していない。
`doc/RPC.md` / `doc/SUPPORT.md` へのリンクは 0 件。
利用者は develop で追加された HTTP RPC / サポート情報のドキュメントに気付けない。

`doc/UI.md` は UI リバースプロキシの廃止 (`Revert "remote-ui 対応 (#75)"`) に伴い削除済みのため、対象外とする。

### FAQ の壊れたリンク

`doc/FAQ.md` の以下の記述にリンクの誤りが 3 つある。

- リンクテキストが `--openH264` と大文字 (実装は `src/util.cpp` の `Util::ParseArgs` が定義する `--openh264`)
- URL のブランチ名が `master` (zakuro のデフォルトブランチは `develop`。`master` ブランチは残存しているが
  内容が古く、現行のドキュメントと乖離している)
- `doc/` 内の別ドキュメント (USE.md) へのリンクなのに、相対リンクではなく GitHub の絶対 URL を使用
  (`https://github.com/shiguredo/zakuro/blob/master/doc/USE.md#openh264`)

## 設計方針

- `README.md` には目次が無いため、`## FAQ` 節と `## ヘルプ` 節の間に新規節として以下を追加する
  - `## サポート` → `[SUPPORT.md](doc/SUPPORT.md)`
  - `## HTTP RPC` → `[RPC.md](doc/RPC.md)`
- `doc/FAQ.md` の `--openH264` リンクを以下に修正
  - リンクテキスト: `--openh264`
  - URL: `USE.md#openh264` (相対リンク)
- `doc/` 配下のドキュメントに `master` を参照するリンクが無いか一括確認する

## 完了条件

- `README.md` から `RPC.md` / `SUPPORT.md` の全てにリンクがあること
- `doc/FAQ.md` の `--openH264` リンクが機能すること (相対リンク、正しい大文字小文字)
- `doc/` 配下から `master` ブランチを参照するリンクが 0 件になること

## 解決方法

`README.md` の `## FAQ` 節と `## ヘルプ` 節の間に `## サポート` と `## HTTP RPC` の
節を追加し、それぞれ `doc/SUPPORT.md` と `doc/RPC.md` へリンクした。

`doc/FAQ.md` の OpenH264 のリンクを次のように直した。

- リンクテキストを `--openH264` から `--openh264` に修正 (`src/util.cpp` の
  `Util::ParseArgs` が定義するオプション名と一致させる)
- URL を `https://github.com/shiguredo/zakuro/blob/master/doc/USE.md#openh264` から
  相対リンク `USE.md#openh264` に変更 (デフォルトブランチは `develop` であり、
  `doc/` 内の別ドキュメントへのリンクは相対リンクにする)

`doc/UI.md` は UI リバースプロキシの廃止に伴い削除済みのため対象外とした。

検証したこと:

- `README.md` から `doc/RPC.md` と `doc/SUPPORT.md` へのリンクがあること
- `README.md` と `doc/*.md` に `blob/master` を参照するリンクが 0 件であること
- `--openH264` の誤記が 0 件であること
- リンク先の `doc/RPC.md` / `doc/SUPPORT.md` / `doc/USE.md` / `doc/FAQ.md` が存在すること
- `doc/USE.md` に `### OpenH264` の見出しがあり、アンカー `#openh264` が解決すること
- `uvx prek run --files README.md doc/FAQ.md` が pass すること

`CHANGES.md` の `## develop` の `### misc` に `[UPDATE]` のエントリを追加した。
