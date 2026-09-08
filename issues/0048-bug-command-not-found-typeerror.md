# cmd / cmdcap が存在しないコマンドで TypeError になるのを分かりやすくする

- Created: 2026-09-08
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-command-not-found-error
- Polished: 2026-09-09

## 目的

`buildbase.py` の `cmd` は `resolve=True` のデフォルトで `shutil.which(args[0])` の結果を先頭に置換する。
存在しないコマンドを渡すと `None` が先頭に入り、`subprocess.run` が `TypeError` を出す。
エラーメッセージに原因のコマンド名が含まれず、診断に時間がかかる。

## 現状

`buildbase.py::cmd` (buildbase.py) は `args = [shutil.which(args[0]), *args[1:]]` を実行し、
`subprocess.run(args, check=True)` へ渡す。コマンドが PATH に無い場合 (例: 未インストールの `cmake`) は
`[None, ...]` になり `TypeError: expected str, bytes or os.PathLike object, not NoneType` で終了する。
`cmdcap` は `cmd` を内部で呼ぶため同じ影響を受ける。

## 設計方針

- `buildbase.py` は melpon/buildbase のコピーテンプレートであり、更新時に上流から上書きされるため編集しない
  (issue 0039 の方針に従う)
- `run.py` 側に `cmd` / `cmdcap` のラッパーを導入し、呼び出し前に `shutil.which(args[0])` の結果を検証する。
  `None` の場合はコマンド名を含む `RuntimeError(f"command not found: {args[0]}")` を出して終了する
- ラッパーが対象とするのは run.py から直接呼び出す `cmd` / `cmdcap` のみとする。
  `buildbase.py` 内部から呼び出される `cmd` (例: `download` 内の `curl` / `wget`、
  `git_clone_shallow` 内の `git`、`install_openh264` 内の `make`) は buildbase を編集しない方針のため
  対象外であり、それらが存在しない場合の `TypeError` は本 issue では直さない

## 完了条件

- run.py から直接呼び出す `cmd` / `cmdcap` に存在しないコマンドを渡した際に、
  コマンド名を含む明確なエラーメッセージで終了すること
- `run.py build` などの正常系の挙動が変わらないこと
