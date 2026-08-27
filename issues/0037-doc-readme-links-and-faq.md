# README から RPC.md / UI.md / SUPPORT.md へのリンク追加と FAQ の壊れたリンクを修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-readme-links-and-faq
- Polished: {YYYY-MM-DD}

## 目的

`README.md` から `doc/RPC.md` / `doc/UI.md` / `doc/SUPPORT.md` へのリンクが 1 個もなく、
develop で追加した新機能のドキュメントが「宙に浮いた」状態になっている。
`doc/FAQ.md` にも壊れたリンクが残っている。ドキュメントの導線を修正する。

## 現状

### README のリンク欠落

`README.md` からは `doc/USE.md` / `doc/BUILD.md` / `doc/FAQ.md` しか参照していない。
`doc/RPC.md` / `doc/UI.md` / `doc/SUPPORT.md` へのリンクは 0 件。
利用者は develop で追加された HTTP RPC / UI プロキシ / サポート情報のドキュメントに気付けない。

### FAQ の壊れたリンク

`doc/FAQ.md` の以下の記述にリンクの誤りが 3 つある。

- リンクテキストが `--openH264` と大文字 (実装は `--openh264`)
- URL のブランチ名が `master` (zakuro のデフォルトブランチは `develop`)
- 同ページ内リンクなのに絶対 URL を使用 (`https://github.com/shiguredo/zakuro/blob/master/doc/USE.md#openh264`)

## 設計方針

- `README.md` の目次または「## FAQ」節付近に、以下を追加する
  - `## サポート` → `[SUPPORT.md](doc/SUPPORT.md)`
  - `## HTTP RPC` → `[RPC.md](doc/RPC.md)`
  - `## UI プロキシ` → `[UI.md](doc/UI.md)`
- `doc/FAQ.md` の `--openH264` リンクを以下に修正
  - リンクテキスト: `--openh264`
  - URL: `USE.md#openh264` (相対リンク)
- 他のドキュメント間のリンクも `master` 参照が無いか一括確認する

## 完了条件

- `README.md` から `RPC.md` / `UI.md` / `SUPPORT.md` の全てにリンクがあること
- `doc/FAQ.md` の `--openH264` リンクが機能すること (相対リンク、正しい大文字小文字)
- `doc/` 配下から `master` ブランチを参照するリンクが 0 件になること
