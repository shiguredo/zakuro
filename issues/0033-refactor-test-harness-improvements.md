# test/ ハーネス改善 (Python 3.14 要求緩和・subprocess.PIPE drain・pytest 未使用依存)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-test-harness-improvements
- Polished: {YYYY-MM-DD}

## 目的

`test/` のハーネスに以下の問題があり、CI 導入や長時間テストで実際にハマる。まとめて修正する。

- `test/pyproject.toml` の `requires-python = ">=3.14"` が厳しすぎる (CI runner に入っていない)
- `test/zakuro.py` の `subprocess.PIPE` stderr を drain しておらず、長時間テストで hang する
- `test/pyproject.toml` の `pytest-timeout` / `pytest-repeat` が依存として入っているが未使用
- 意図不明な `time.sleep(2)` / `time.sleep(0.2)` が残っている

## 現状

### Python 3.14 要求

`test/pyproject.toml` は `requires-python = ">=3.14"`。Python 3.14 は 2025 年 10 月リリースで、
現時点で CI runner (ubuntu-24.04) には入っていない。開発者が手動で実行するにも環境縛りが厳しい。

### subprocess.PIPE stderr 未 drain

`test/zakuro.py` の `Zakuro.__enter__` は `stderr=subprocess.PIPE, text=True` で起動し、
`_wait_for_startup` の失敗時のみ `stderr.read()` する。
Zakuro は verbose ログを stderr に出す可能性があり、パイプバッファ (Linux で通常 64KB) を超えると
プロセスが書き込みでブロックする。E2E テストで長時間実行するテストを追加した瞬間 deadlock する。

### 未使用依存

`test/pyproject.toml` の `dev` グループに `pytest-timeout` / `pytest-repeat` が入っているが、
`test/test_zakuro.py` にマーカが 1 個もない。導入意図が不明。

### 意図不明な sleep

- `test/zakuro.py` の `_wait_for_startup` 先頭で 2 秒 sleep してから HTTP ヘルスチェック開始
- `test/zakuro.py` の terminate 完了後にさらに 0.2 秒 sleep

理由がコメントされておらず、削除して良いか判断がつかない。

## 設計方針

- `requires-python` を `>=3.12` などに緩めるか、3.14 を要求する理由を README (test/README.md) に明記する
- stderr を別スレッドで drain するか、`stderr=subprocess.DEVNULL` に変更する
  (エラーメッセージが失われる問題は log ファイル経由で回収するように改める)
- 未使用の `pytest-timeout` / `pytest-repeat` は削除するか、使うテストを書く
- 意図不明な sleep はコメントを付ける、または削除する
- `httpx.Client(timeout=...)` の設定を統一する
- `Zakuro.__enter__` の例外時に `self._http_client` の close が漏れる問題を修正する (`_cleanup` から close する)

## 完了条件

- CI runner (ubuntu-24.04) で `pytest` が動くこと
- 長時間テスト (10 分以上) を追加しても `subprocess.PIPE` バッファ full で hang しないこと
- pytest の未使用依存が削除されていること
- sleep に意図コメントが付いているか、削除されていること
