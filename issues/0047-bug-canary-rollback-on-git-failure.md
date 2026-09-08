# canary.py の git 操作途中失敗時に VERSION 更新コミットが残るのを修正する

- Created: 2026-09-08
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-canary-rollback
- Polished: 2026-09-08

## 目的

`canary.py` の `git_operations` は `git commit` / `git tag` / `git push` を順次実行し、途中で失敗するとそこで終了する。
例えば既存タグと同じ名前のタグ付けに失敗すると、VERSION 更新コミットだけがリポジトリに残り、手動での巻き戻しが必要になる。

## 現状

`canary.py` の `git_operations` (canary.py) は `subprocess.run(..., check=True)` で 4 つの git 操作を順次実行しており、
途中失敗時のロールバックが無い。`write_version_file` が VERSION を書き換えたあとに `git commit -am` するため、
`git tag` 失敗時に残るコミットには VERSION 更新が含まれる。
また `git commit -am` は追跡中の全変更をコミットするため、作業ツリーに他の変更があるとそれも巻き込まれてしまう。

## 設計方針

- `write_version_file` による VERSION の書き換え前に、作業ツリーがクリーンであることを
  `git status --porcelain` で確認し、クリーンでなければ何も変更せずエラーメッセージを出力して終了する
- `git tag` / `git push` の失敗時は、HEAD のコミットが canary のものであること
  (例: `git log -1 --format=%s` の結果が `[canary] Update VERSION` であること) を確認してから、
  そのコミットがリモートに push されていない場合に限り `git reset --hard HEAD~1` で巻き戻す
- ブランチ push 成功後にタグ push が失敗した場合は、コミットがリモートに反映済みのため
  巻き戻さず、その旨をエラーメッセージに含めて終了する
- 巻き戻す際に、この実行で作成したタグが残っていれば `git tag -d` で削除する
- 失敗とロールバックの結果を明確なエラーメッセージで出力して終了する
- `--dry-run` 時は実際の操作を行わない (既存の dry-run 出力を維持する)

## 完了条件

- `git tag` が既存タグで失敗した場合に、VERSION 更新コミットが残らないこと
- 巻き戻した場合に、この実行で作成したタグも残らないこと
- 作業ツリーがクリーンでない場合に、何も変更せず終了すること
- ロールバックを実行した際に、その旨が明確なエラーメッセージで出力されること
