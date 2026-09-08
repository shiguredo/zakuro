# README のヘルプ抜粋に develop の HTTP / UI オプションを反映する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/add-readme-help-http-ui-options
- Polished: 2026-09-08
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
方式は `zakuro --help` の実行結果をそのまま貼り付ける。
`--http-host` / `--http-port` / `--ui` / `--ui-remote-url` を含む最新のヘルプに置き換える。
再生成により、実装に存在しない `--port` の 2 行（issues/0017 で扱う）も自然に削除される。

CLI11 のヘルプ出力にはコメント行を挿入できない。実装 (`src/util.cpp`) の
「アプリケーション全体の共通オプション」と「インスタンス毎のオプション」の境界を示すコメント行を
抜粋に挿入すると、完了条件の「`zakuro --help` の実際の出力と一致」を満たせなくなるため挿入しない。

## 完了条件

- `README.md` のヘルプ抜粋に `--http-host` / `--http-port` / `--ui` / `--ui-remote-url` が記載されていること
- `zakuro --help` の実際の出力と README の抜粋が一致していること
- `README.md` の「ヘルプ」セクションに、抜粋が `zakuro --help` の出力を反映している旨と、
  オプションの追加・変更・削除時には `zakuro --help` を再実行して抜粋を更新する旨の注記が書かれていること
  （注記は抜粋ブロックの外に置き、抜粋自体はヘルプ出力と一致させる）
