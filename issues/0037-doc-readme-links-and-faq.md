# README から RPC.md / UI.md / SUPPORT.md へのリンク追加と FAQ の壊れたリンクを修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-readme-links-and-faq
- Polished: 2026-09-08

## 目的

`README.md` から `doc/RPC.md` / `doc/UI.md` / `doc/SUPPORT.md` へのリンクが 1 個もなく、
develop で追加した JSON-RPC / UI リバースプロキシ機能のドキュメント (`doc/RPC.md` / `doc/UI.md`) が
README から参照されない「宙に浮いた」状態になっている。`doc/SUPPORT.md` も README からは参照されていない。
`doc/FAQ.md` にはリンクテキストが実装と食い違うリンクが残っている。ドキュメントの導線を修正する。

## 現状

### README のリンク欠落

`README.md` からは `doc/USE.md` / `doc/BUILD.md` / `doc/FAQ.md` しか参照していない。
`doc/RPC.md` / `doc/UI.md` / `doc/SUPPORT.md` へのリンクは 0 件。
利用者は develop で追加された HTTP RPC / UI プロキシ / サポート情報のドキュメントに気付けない。

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
  - `## UI プロキシ` → `[UI.md](doc/UI.md)`
- `doc/FAQ.md` の `--openH264` リンクを以下に修正
  - リンクテキスト: `--openh264`
  - URL: `USE.md#openh264` (相対リンク)
- `doc/` 配下のドキュメントに `master` を参照するリンクが無いか一括確認する

## 完了条件

- `README.md` から `RPC.md` / `UI.md` / `SUPPORT.md` の全てにリンクがあること
- `doc/FAQ.md` の `--openH264` リンクが機能すること (相対リンク、正しい大文字小文字)
- `doc/` 配下から `master` ブランチを参照するリンクが 0 件になること
