# canary.py の git 操作途中失敗時に VERSION 更新コミットが残るのを修正する

- Created: 2026-09-08
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-canary-rollback
- Polished: {YYYY-MM-DD}

## 目的

`canary.py` の `git_operations` は `git commit` / `git tag` / `git push` を順次実行し、途中で失敗するとそこで終了する。
例えば既存タグと同じ名前のタグ付けに失敗すると、VERSION 更新コミットだけがリポジトリに残り、手動での巻き戻しが必要になる。

## 現状

`canary.py` の `git_operations` (canary.py) は `subprocess.run(..., check=True)` で 4 つの git 操作を順次実行しており、
途中失敗時のロールバックが無い。`write_version_file` が VERSION を書き換えたあとに `git commit -am` するため、
`git tag` 失敗時に残るコミットには VERSION 更新が含まれる。
また `git commit -am` は追跡中の全変更をコミットするため、作業ツリーに他の変更があるとそれも巻き込まれてしまう。

## 設計方針

- `git tag` / `git push` の失敗時に、直前の `git commit` (canary が作成したコミット) を
  `git reset --hard HEAD~1` で巻き戻す
- 巻き戻し前に、対象コミットが canary のものであることを確認してから実行する
  (canary 実行前の作業ツリーがクリーンであることを `git status --porcelain` で確認するなどの防御を入れる)
- 失敗とロールバックの結果を明確なエラーメッセージで出力して終了する
- `--dry-run` 時は実際の操作を行わない (既存の dry-run 出力を維持する)

## 完了条件

- `git tag` が既存タグで失敗した場合に、VERSION 更新コミットが残らないこと
- ロールバックを実行した際に、その旨が明確なエラーメッセージで出力されること
