# README のヘルプ抜粋に develop で追加した HTTP / UI オプションを追記する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/add-readme-help-http-ui-options
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`README.md` の埋め込みヘルプ抜粋に、develop で追加された HTTP サーバー系・UI 系オプションが
1 個も載っていない。CHANGES.md では明確に追加宣言しているのに、利用者は README を見ても存在に気付けない。
実装 (`src/util.cpp`) の該当オプションを README に反映する。

## 現状

`src/util.cpp` の `Util::ParseArgs` は以下のオプションを追加している。

- `--http-host <TEXT>`
- `--http-port <INT: [1-65535]>`
- `--ui`
- `--ui-remote-url <TEXT>`

`README.md` の埋め込みヘルプ抜粋にはこれらが 1 個も載っていない。
`CHANGES.md ## develop` では以下が追加宣言されている。

- `[ADD] HTTP サーバー機能を追加する`
- `[ADD] --ui オプションで Zakuro UI を有効化する機能を追加する`
- `[ADD] ヘルスチェック用 HTTP API /.ok を追加する`
- `[ADD] JSON-RPC 2.0 エンドポイント /rpc を追加する`
- `[ADD] JSON-RPC メソッド GetVersion を追加する`
- `[ADD] JSON-RPC 2.0 の Notification（id なしリクエスト）に対応する`

## 設計方針

`README.md` のヘルプ抜粋を、実装 (`src/util.cpp`) の `add_option` / `add_flag` に基づいて再生成する。
最も簡便なのは `zakuro --help` の実行結果をそのまま貼り付ける方式。
`--http-host` / `--http-port` / `--ui` / `--ui-remote-url` を含む最新のヘルプに置き換える。

同時に「アプリ全体オプション」と「インスタンス毎のオプション」の境界がヘルプから読み取れるよう、
コメント行を挿入することも検討する（実装 (`src/util.cpp`) にはコメントとして既に境界がある）。

## 完了条件

- `README.md` のヘルプ抜粋に `--http-host` / `--http-port` / `--ui` / `--ui-remote-url` が記載されていること
- `zakuro --help` の実際の出力と README の抜粋が一致していること
- 動作環境変更や別の新オプション追加時にヘルプ抜粋を更新するフローがドキュメントに書かれていること
