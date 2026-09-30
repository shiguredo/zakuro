# test/ ハーネス改善 (Python 3.14 要求緩和・subprocess.PIPE drain・pytest 未使用依存)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-test-harness-improvements
- Polished: 2026-09-08

## 目的

`test/` のハーネスに以下の問題があり、CI 導入や長時間テストで実際にハマる。まとめて修正する。

- `test/pyproject.toml` の `requires-python = ">=3.14"` が厳しすぎる (CI runner の既定 `python3` は 3.12)
- `test/zakuro.py` の `subprocess.PIPE` stderr を drain しておらず、長時間テストで hang する
- `test/pyproject.toml` の `pytest-timeout` / `pytest-repeat` が依存として入っているが未使用
- 意図不明な `time.sleep(2)` / `time.sleep(0.2)` が残っている

## 現状

### Python 3.14 要求

`test/pyproject.toml` は `requires-python = ">=3.14"`。加えて `test/` の `.python-version` が `3.14`、`uv.lock` の `requires-python` も `>=3.14` であり、Python 3.14 固定になっている。
`test/zakuro.py` / `test/conftest.py` / `test/test_zakuro.py` を確認した限り、Python 3.14 固有の文法や標準ライブラリは使われていない (使えるのは `typing.Self` の 3.11 以降、`dict[str, Any] | None` の 3.10 以降まで)。
CI runner (ubuntu-24.04) の既定 `python3` は 3.12 のため、`uv run pytest` は `.python-version` に従い Python 3.14 を探しに行く。runner に 3.14 が無ければダウンロードに頼り、開発者のローカル環境も 3.14 の入手状況に依存する。
E2E テストの実行だけのために最新版の 3.14 が必要になるのは過剰であり、3.12 で十分である。

なお、`.github/workflows/build.yml` は現在 pytest を実行しないため、本 issue 自体は CI を壊さない (CI への pytest 組み込みは issue 0035 の対象)。

### subprocess.PIPE stderr 未 drain

`test/zakuro.py` の `Zakuro.__enter__` は `stderr=subprocess.PIPE, text=True` で起動し、
`_wait_for_startup` の失敗時のみ `stderr.read()` する。
`_wait_for_startup` のヘルスチェックループ中に stderr を読んでいないため、Zakuro が verbose ログを stderr に出す場合
(`--log-level verbose`)、パイプバッファ (Linux で通常 64KB) を超えるとプロセスが書き込みでブロックする。
長時間の E2E テストで verbose ログが増えると deadlock する。

### 未使用依存

`test/pyproject.toml` の `dev` グループに `pytest-timeout` / `pytest-repeat` が入っているが、
`test/test_zakuro.py` にマーカが 1 個もない。導入意図が不明。

### 意図不明な sleep

- `test/zakuro.py` の `_wait_for_startup` 先頭で 2 秒 sleep してから HTTP ヘルスチェック開始
- `test/zakuro.py` の terminate 完了後にさらに 0.2 秒 sleep

`time.sleep(2)` には「プロセスが完全に起動するまで少し待機」程度のコメントしかなく、2 秒という値の根拠がない。
`time.sleep(0.2)` はコメントが無い。どちらも削除して良いか判断がつかない。

### httpx.Client のタイムアウト設定

`test/zakuro.py` の `Zakuro.__enter__` は `httpx.Client(timeout=10.0)` を作成して RPC クライアントに渡す一方、
`_wait_for_startup` は自前の `httpx.Client()` を作成し `client.get(url, timeout=5)` でヘルスチェックする。
どちらのタイムアウトも意図がコメントされておらず、値が散在している。

### HTTP クライアントの close 漏れ

`Zakuro.__enter__` は `_wait_for_startup` 成功後に `self._http_client` と `self._rpc` を作成する。
`__exit__` は `self._http_client.close()` してから `_cleanup()` を呼ぶため通常経路は正しいが、
`__enter__` 内の例外経路 (例: `_wait_for_startup` が raise) では `_cleanup()` は `self._http_client` を close しない。
例外経路で `self._http_client` が未作成なら実害は無いが、
`httpx.Client` 作成後に例外が出るコードを足すと close 漏れし、リソース解放の責務が `_cleanup` / `__exit__` に分散している。

## 設計方針

- `requires-python` を `>=3.12` に緩め、`.python-version` を `3.12` に、`uv.lock` を 3.12 で再生成する
  - 理由: CI runner (ubuntu-24.04) の既定 `python3` が 3.12 であること、テスト基盤に 3.14 固有の機能が無いこと、
    `uv run pytest` が Python 3.14 のダウンロードや CI runner の工具 (toolcache) に依存せず動く構成にするため
- stderr を別スレッドで drain するか、`stderr=subprocess.DEVNULL` に変更する
  - `stderr=subprocess.DEVNULL` の場合は `_wait_for_startup` の `stderr.read()` を削除し、エラーメッセージを知る手段として zakuro 側の log ファイル (`main.cpp` の `FileRotatingLogSink` が出力する `webrtc_logs_*`) があることをハーネス内コメントに明記する
  - drain する構成では `_wait_for_startup` の失敗時に読み出せるようバッファしておくこと
  - どちらの案でも `_wait_for_startup` のヘルスチェック遅延 (`time.sleep(2)` / `time.sleep(0.5)`) の見直しと合わせて行うこと
- 未使用の `pytest-timeout` / `pytest-repeat` は削除するか、使うテストを書く
  - 使うテストが現状に無いので、原則削除する。E2E で timeout をかけたい場合は 0043 (E2E テスト拡充) の段階で再導入する
- 意図不明な sleep はコメントを付ける、または削除する
  - `time.sleep(2)` はヘルスチェックループが 0.5 秒間隔で接続確認しているため、先頭の固定待機は削除できる (削除すると起動待ちを早く終えられる)
  - `time.sleep(0.2)` は terminate を待ち終えた直後の待機であり、残すなら意図 (プロセス終了の反映待ちなど) をコメントし、不要と判断したら削除する
- `httpx.Client(timeout=...)` の設定を整理する
  - `test/zakuro.py` では `Zakuro.__enter__` の `httpx.Client(timeout=10.0)` (RPC クライアント用) と `_wait_for_startup` 内の `client.get(url, timeout=5)` (ヘルスチェック用) でタイムアウト値が異なる。用途ごとに値を定め、コメントで意図を明記する。RPC 用の 10 秒は GetVersion などが安定して返るまでの猶予、ヘルスチェック用の 5 秒は起動待ちループを早く回すための短めのタイムアウト、という整理を想定している
- `Zakuro.__enter__` の例外時に `self._http_client` の close が漏れ得る点を修正する
  - `_cleanup` 内で `self._http_client` を close する。`__exit__` 側は close 済みなので二重 close にならないよう close 後に `None` に戻す

## 完了条件

- `test/` 配下が Python 3.12 で実行できること (`requires-python` / `.python-version` / `uv.lock` がすべて 3.12 系に揃っており、`uv run pytest` が Python 3.14 のダウンロード無しで動くこと)
- `test/zakuro.py` を長時間実行しても `subprocess.PIPE` バッファ full で hang しないこと (stderr を drain していること)
- pytest の未使用依存が削除されていること
- sleep に意図コメントが付いているか、削除されていること
- `__enter__` 例外経路と `__exit__` で HTTP クライアントの close が漏れの無い形で行われること
