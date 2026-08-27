# run.py に test / canary サブコマンドを追加し format.sh と統合する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-run-py-subcommands
- Polished: {YYYY-MM-DD}

## 目的

`run.py` にサブコマンドが `build` と `format` しかなく、`test` / `canary` などのプロジェクトオペレーションは
別ルート (未整備 or `canary.py` 直接実行 or CI から) で叩く必要がある。
`format.sh` と `run.py format` の 2 系統が並立しているのも冗長。整理する。

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

### canary.py のロールバック無し

`canary.py` は `subprocess.run(..., check=True)` で git 操作を順次実行するが、
途中失敗時のロールバックが無い。`git commit` 成功 → `git tag` 失敗 (既存タグ) で commit だけ残る。

### buildbase.py cmd の nullable arg

`buildbase.py::cmd` は `resolve=True` のデフォルトで `shutil.which(args[0])` の結果を先頭に置換。
存在しないコマンドを指定すると `[None, ...]` になり `TypeError` (エラーメッセージが分かりにくい)。

## 設計方針

- `run.py` に `test` サブコマンドを追加。`cd test && uv run pytest ...` 相当を呼び出す
- `run.py` に `canary` サブコマンドを追加、または `canary.py` の使い方を README に明記
- `canary.py` に git 操作のロールバック (`try/except` で `git reset --hard HEAD~1`) を追加
- `format.sh` を廃止し `run.py format` に統一 (`.mm` 対応を移植)
- `buildbase.py::cmd` の先頭に `if shutil.which(args[0]) is None: raise RuntimeError(f"command not found: {args[0]}")` を追加

## 完了条件

- `python3 run.py test` で pytest が実行できること
- `python3 run.py canary` またはドキュメントで canary リリース手順が明示されていること
- `format.sh` が削除され、`run.py format` に統一されていること
- 存在しないコマンドを叩いたときに明確なエラーメッセージが出ること
