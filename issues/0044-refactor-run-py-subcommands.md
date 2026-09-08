# run.py に test / canary サブコマンドを追加し format.sh と統合する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-run-py-subcommands
- Polished: 2026-09-08

## 目的

`run.py` にサブコマンドが `build` と `format` しかなく、`test` / `canary` を `run.py` から起動できない。
`test` は起動手段が無く、`canary` は `canary.py` を直接実行する必要がある。
`format.sh` と `run.py format` の 2 系統が並立しているのも冗長。`run.py` にサブコマンドを集約して整理する。

## 現状

### test サブコマンド無し

`run.py` に `test` サブコマンドが存在せず、`test/uv.lock` / `test/pyproject.toml` / `test/test_zakuro.py` が
定義されているのに誰も起動しない。CHANGES.md `misc` に「E2E テストを追加」と書いてあるが実態と乖離。

### canary サブコマンド無し

`canary.py` は独立スクリプトとして `python3 canary.py` で直接実行する設計。
`run.py canary` サブコマンドは存在しない。ドキュメント (README / doc/BUILD.md) にも `canary.py` の使い方が無い。

### format.sh 並立

`format.sh` (bash + clang-format) と `run.py format` (Python + clang-format) が同じ処理を 2 通り実装。
差もある (format.sh は `.mm` を含むが `run.py` は `.h`/`.cpp` のみ)。二重管理で挙動が乖離するリスク。

## 設計方針

- `run.py` に `test` サブコマンドを追加。`test/` で `uv run pytest` を実行する。
  実行にはビルド済みバイナリが必要 (test/zakuro.py の `Zakuro` が `_build/` 配下の実行ファイルを参照)。
  `sora_config` フィクスチャ依存テストは `TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` /
  `TEST_SECRET_KEY` が未設定だと skip される
- `run.py` に `canary` サブコマンドを追加。`canary.py` の `main` 相当の処理
  (VERSION 更新・git タグ付け・push) を実行できるようにする
- `format.sh` を廃止し `run.py format` に統一 (`.mm` 対応を移植)

## 完了条件

- `python3 run.py test` で `test/` の pytest が起動すること (Sora 接続に依存するテストは
  `TEST_SIGNALING_URLS` 等の環境変数が未設定だと skip されること)
- `python3 run.py canary` で canary リリース操作 (VERSION 更新・タグ付け・push) が実行できること
- `format.sh` が削除され、`run.py format` に統一されていること
