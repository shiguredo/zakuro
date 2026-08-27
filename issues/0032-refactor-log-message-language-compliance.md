# AGENTS.md 規約違反のログメッセージ言語を修正する (本体は英語・テストは日本語)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-log-message-language-compliance
- Polished: {YYYY-MM-DD}

## 目的

AGENTS.md の以下 2 規約に違反するログメッセージが本体・テスト両方に散在しているため、
規約に合わせて修正する。

- 「ログメッセージは全て英語にすること」
- 「テストのログメッセージは全て日本語にすること」

## 現状

### 本体側 (英語であるべきなのに日本語)

`src/main.cpp` の複数箇所。

- `std::cerr << "getrlimit 失敗" << std::endl;`
- `std::cerr << "ファイルディスクリプタの数が足りません。..."`
- `std::cerr << "instances キーがありません。"`
- `std::cerr << "instances の下に設定がありません。"`
- `std::cerr << "--ui-remote-url を指定する場合は --ui も指定してください"`
- `std::cerr << "--http-host と --http-port は両方指定する必要があります"`

`src/y4m_reader.cpp` の `std::cout << header << std::endl;` (毎回吐かれるデバッグ痕跡)

`src/main.cpp` の JSONC 経由生成 arg 列を `std::cout` に流している箇所 (ログレベル非制御)

### テスト側 (日本語であるべきなのに英語)

`test/zakuro.py` の 10 箇所超の `print()`。

- `print(f"Starting zakuro: ...")`
- `print(f"Started zakuro process with PID: ...")`
- `print(f"Cleaning up due to exception: ...")`
- `print(f"Waiting for HTTP server on port ...")`
- `print(f"Zakuro started successfully")`
- `print(f"Connection error: ...")`
- `print(f"HTTP error: ...")`
- `print(f"Terminating zakuro process")`
- `print(f"Force killing zakuro process")`

`test/conftest.py` の 3 箇所の `pytest.skip("...")`。

- `pytest.skip("TEST_SIGNALING_URLS environment variable is required")`
- 他 2 箇所

## 設計方針

- 本体側の日本語ログメッセージをすべて英語に翻訳する
  - 例: `"File descriptor limit is too small; must be >= 1024"`
  - 例: `"instances key is missing"`
  - 例: `"--ui-remote-url requires --ui"`
- `RTC_LOG` 経由に統一するのが望ましい (`std::cerr` / `std::cout` は極力減らす。ただしプロセス起動時の early error は `std::cerr` が妥当)
- テスト側の英語メッセージをすべて日本語に翻訳する
  - 例: `print("zakuro を起動: ...")`
  - 例: `pytest.skip("環境変数 TEST_SIGNALING_URLS が必要です")`
- `y4m_reader.cpp` の `std::cout << header` は削除、または `RTC_LOG(LS_INFO)` に格上げして必要時のみ出す
- `main.cpp` の JSONC 経由 arg 列出力も `RTC_LOG(LS_INFO)` へ移動するか、`--verbose` フラグで制御

## 完了条件

- `src/` 配下に日本語ログメッセージが 0 件になること (`git grep` で確認)
- `test/` 配下に英語 `print` / `skip` メッセージが 0 件になること
- ログレベル非制御の `std::cout` / `std::cerr` が発火頻度の高い場所に残っていないこと
