# テストの拡充 (E2E テストと C++ 単体テスト基盤の導入、test_version 1 個からの脱却)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/add-e2e-coverage
- Polished: 2026-09-08
- Updated: 2026-09-29

## 目的

`test/` には `test_zakuro.py` の `test_version` と `test_client_cert.py` の 20 ケースがあるが、
Zakuro の主要責務である「N VC を張って負荷をかける」経路、DataChannel シグナリングでの切断、
`--duration` + `--repeat-interval` での切断・再接続、JSON-RPC のエラーコード、`--config` の各キー、
SIGINT での終了などが未カバー。
CHANGES.md `misc` の `[ADD] pytest を使った E2E テストを追加する` の実態を強化する。

テストの実行方法は実バイナリを起動する pytest に一本化する (issues/0066)。
0066 で CTest と C++ のテスト実行ファイル 2 本を撤去したため、撤去で失った検証
(ADM の `Init` の再入、`GameKeyCore` の競合) の扱いを本 issue で決める。

## 現状

- `test/test_zakuro.py` の実テストは `test_version(sora_config, free_port)` 1 個。
  `sora_config` を要求するため環境変数が無い場合は skip される (実 Sora は不要で GetVersion のみだが、
  フィクスチャ経由で環境変数に依存している点は issues/0035 が扱う)
- `test/test_client_cert.py` に mTLS の E2E 20 ケースがある (issues/0056 / 0057 / 0058、2026-09-26)。
  `test/zakuro.py` の `Zakuro` クラスはそこで広く使われているが、`--duration` /
  `--repeat-interval` / `--sora-data-channel-signaling` を使うテストは無い
- `test/zakuro.py` の `Zakuro._cleanup` は `terminate()` (SIGTERM) のみで、SIGINT を送る手段が無い
- C++ 単体テスト基盤と CTest は issues/0066 で撤去した (`CMakeLists.txt` にテストターゲットは無く、
  `test/*.cpp` も無い)。テストは実バイナリを起動する pytest に一本化する方針
- `.github/workflows/build.yml` はビルドとパッケージのみで pytest を実行しない
  (CI への pytest 組み込みは issues/0035 が担当)

## 設計方針

以下のテストを段階的に追加する (優先度順)。いずれも pytest と `test/zakuro.py` のラッパーで
実バイナリを起動して検証する。issues/0066 で CTest を撤去したため、C++ のテスト実行ファイルは使わない。

1. (E2E) **DataChannel シグナリング + 切断のクラッシュ回帰テスト** (issue 0002 の完了条件と噛み合う)
   - `--sora-data-channel-signaling` + `--duration 5 --repeat-interval 1 --vcs 2` を数分回した後
     SIGINT で終了させ、セグフォや異常終了がないことを確認する
   - `--repeat-interval 1` の場合、`src/zakuro.cpp` の `add_reconnect_scenario` は Exit を積まずに
     切断後に再接続を繰り返すため、プロセスは自然終了しない。数分実行後に SIGINT を送る
   - `test/zakuro.py` には SIGINT を送る手段が無いため、ハーネスに追加する
   - Sora C++ SDK は `DEPS` で `2026.2.1` に更新済み (issue 0002、2026-09-28)。issue 0002 は
     未クローズのため、依存として残す場合はクローズ待ちとする
   - 数分規模の実行には timeout を使う。`pytest-timeout` は現在 `test/pyproject.toml` の dev 依存に
     含まれているため、issues/0033 で削除された場合は本 issue で再導入する
2. (E2E) **ADM ライフサイクルの回帰テスト** (issue 0003 / 0004 の回帰防止)
   - issues/0004 の完了条件 (`Create → Init → Init（2 回目）→ RegisterAudioCallback →
     StartRecording → StopRecording → Terminate → 破棄` を 1000 回) は `zakuro_adm_test` として
     実装されたが、issues/0066 で撤去された
   - pytest ではプロセス内部の `Init` の再入を再現できない。実バイナリを音声ありの設定で起動し、
     RPC が応答し、SIGINT で正常終了することを繰り返し確認する範囲に置き換える
   - 二重 `Init` という条件そのものはカバーできない。対象外とするか別 issue に切り出すかを
     実装時に決めて明記する
3. (E2E) **VirtualClient::Close の 2 段階呼び出し状態遷移テスト** (issue 0024)
   - 内部の状態遷移は pytest から直接観測できない。issue 0024 の実装後、切断と再接続の挙動として
     E2E で観測できる形にするか、対象外にするかを決める
4. (E2E) **JSON-RPC エラーコード網羅テスト**
   - `-32700` (Parse error): `JsonRpcHandler` では生成されず、`src/http_server.cpp` の
     `HttpSession::HandleJsonRpcRequest` (private) が `boost::json::parse` に失敗したときに生成される。
     不正 JSON を `/rpc` へ POST して検証する
   - `-32600` / `-32601` / `-32603` と Notification: `JsonRpcHandler::Process` が生成する。
     `/rpc` 経由で到達できるかを実装時に確認し、到達できないものは検証方法を明記する
5. (E2E) **WavReader / Y4MReader の不正データ・境界値テスト** (issue 0021 / 0022 の回帰防止)
   - `--fake-audio-capture` (WavReader) と Y4M の入力に不正データを渡し、エラーログを出して
     異常終了しないことを確認する
6. (E2E) **`--config` JSONC の設定テスト** (`Util::ParseInstanceToArgs` の網羅)
   - すべてのテストが `--config` 経由で起動するため読込経路自体は通っている。キーごとの網羅を追加する
   - 複数インスタンスの config は `test/test_client_cert.py` の
     `test_client_cert_load_failure_keeps_other_instances` が検証している

本 issue の対象外: シナリオ切替 (`--scenario`)、`--vcs` 100 超の高負荷、SIGINT ハンドリング自体の
仕様検証 (SIGINT による終了経路は項目 1 で通る)。

## 完了条件

- 最低でも上記 1, 2, 4 のテストが追加されていること
  - 1 は実 Sora 接続が必要なため `sora_config` フィクスチャで gate され、環境変数が無い場合は skip される
  - 2 と 4 は実 Sora 不要で、環境変数無しで実行できること
- CI で pytest が実行されること (CI への組み込みは issues/0035 が担当するため、その完了を前提とする)
- 実 Sora 接続が必要なテストは `sora_config` フィクスチャ
  (`TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY` の環境変数) で gate されていること
- 実 Sora 不要なテスト (`test_client_cert.py` の 20 ケース、JSON-RPC のエラーコード、WAV / Y4M、config) は
  環境変数無しで実行できること
