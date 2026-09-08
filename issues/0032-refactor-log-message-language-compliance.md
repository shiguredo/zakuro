# AGENTS.md 規約違反のログメッセージ言語を修正する (本体は英語・テストは日本語)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-log-message-language-compliance
- Polished: 2026-09-08

## 目的

AGENTS.md の以下 2 規約に違反するログメッセージが本体・テスト両方に散在しているため、規約に合わせて修正する。あわせて、ログ経路 (RTC_LOG) に載らずに標準出力へ出続けている診断系の `std::cout` (y4m_reader のヘッダー出力、main.cpp の JSONC 経由 arg 列出力) を整理する。

- 「ログメッセージは全て英語にすること」
- 「テストのログメッセージは全て日本語にすること」

## 現状

### 本体側 (英語であるべきなのに日本語)

`src/main.cpp` の 6 箇所。

- `std::cerr << "getrlimit 失敗" << std::endl;`
- `std::cerr << "ファイルディスクリプタの数が足りません。" "最低でも 1024 以上にして下さい。" << std::endl;`
- `std::cerr << "instances キーがありません。" << std::endl;`
- `std::cerr << "instances の下に設定がありません。" << std::endl;`
- `std::cerr << "--ui-remote-url を指定する場合は --ui も指定してください" << std::endl;`
- `std::cerr << "--http-host と --http-port は両方指定する必要があります" << std::endl;`

`src/y4m_reader.cpp` の `ReadHeader` 内の `std::cout << header << std::endl;` (Y4M ヘッダーを標準出力へ出すデバッグ痕跡。`FakeVideoCapturer` の生成毎に `Y4MReader::Open` から呼ばれるため、インスタンス数分出力される)

`src/main.cpp` の JSONC 経由生成 arg 列を `std::cout` に流している箇所 (ログレベル非制御)

### テスト側 (日本語であるべきなのに英語)

`test/zakuro.py` の 10 箇所の `print()`。

- `print(f"Starting zakuro: {quoted_cmd}")`
- `print(f"Started zakuro process with PID: {self._process.pid}")`
- `print(f"Cleaning up due to exception: {e}")`
- `print(f"Waiting for HTTP server on port {self._http_port}...")`
- `print(f"Zakuro started successfully ({elapsed:.1f}s)")`
- `print(f"  Connection error: {e}")`
- `print(f"  HTTP error: {e}")`
- `print(f"Terminating zakuro process (PID: {pid})")`
- `print(f"Zakuro process (PID: {pid}) terminated")`
- `print(f"Force killing zakuro process (PID: {pid})")`

`test/conftest.py` の 3 箇所の `pytest.skip()`。

- `pytest.skip("TEST_SIGNALING_URLS environment variable is required")`
- `pytest.skip("TEST_CHANNEL_ID_PREFIX environment variable is required")`
- `pytest.skip("TEST_SECRET_KEY environment variable is required")`

## 設計方針

- 本体側の日本語ログメッセージ 6 箇所をすべて英語に翻訳する
  - 例: `"File descriptor limit is too small; must be >= 1024"`
  - 例: `"instances key is missing"`
  - 例: `"--ui-remote-url requires --ui"`
- 上記 6 箇所はユーザー向けの設定・引数エラーなので、`std::cerr` のままとする。`RTC_LOG(LS_ERROR)` へ移すと、デフォルトのログレベル (`src/main.cpp` の `log_level` 初期値 `LS_NONE`。`--log-level` 未指定時) では stderr に出力されずログファイルにしか残らないため、起動失敗時にユーザーが原因を確認できない
- 診断系の出力はログ経路へ移すか削除する (ログレベル非制御の `std::cout` を残さない)
  - `src/y4m_reader.cpp` の `std::cout << header` は削除、または `RTC_LOG(LS_INFO)` に格上げして `--log-level info` 以上のときだけ出力する
  - `src/main.cpp` の JSONC 経由 arg 列出力は `RTC_LOG(LS_INFO)` へ移動するか削除する (`--verbose` フラグは存在しないので、出力を制御したい場合は `--log-level` を使う)
- テスト側の英語 `print()` / `pytest.skip()` メッセージをすべて日本語に翻訳する
  - 例: `print("zakuro を起動: ...")`
  - 例: `pytest.skip("環境変数 TEST_SIGNALING_URLS が必要です")`
- `src/zakuro.cpp` の `ParseDataChannels` の `std::cout << __LINE__` (10 箇所) は issue 0014 の対象であり、本 issue では扱わない

## 完了条件

- `src/` のログ出力 (`std::cout` / `std::cerr` / `RTC_LOG` に渡す文字列) に日本語が 0 件であること。検証時は日本語コメント (AGENTS.md によりコメントは日本語必須のため) をログ出力の判定から除外すること
- `test/` 配下の `print()` / `pytest.skip()` のメッセージがすべて日本語であること (`raise RuntimeError` などの例外メッセージは対象外)
- `src/` の診断系 `std::cout` (y4m_reader のヘッダー出力と main.cpp の JSONC 経由 arg 列出力) が削除されるか、`RTC_LOG(LS_INFO)` でログレベルに従った出力になっていること
- `src/zakuro.cpp` の `ParseDataChannels` の `std::cout` を本 issue で変更していないこと (issue 0014 の対象のため)
