# テストの拡充 (E2E テストと C++ 単体テスト基盤の導入、test_version 1 個からの脱却)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/add-e2e-coverage
- Polished: 2026-09-08

## 目的

`test/test_zakuro.py` の実テストが `test_version` の 1 本のみで、
JSON-RPC の GetVersion を叩いてバージョンを検証するスモークテスト相当しかない。
Zakuro の主要責務である「N VC を張って負荷をかける」経路、DataChannel シグナリングでの切断、
`--duration` + `--repeat-interval` での切断・再接続、`--config` JSONC、SIGINT での終了などが未カバー。
CHANGES.md `misc` の `[ADD] pytest を使った E2E テストを追加する` の実態を強化する。

あわせて、C++ の回帰テストを書けるように単体テスト基盤を導入する。
issues/0004 / 0008 / 0024 / 0026 が C++ 単体テスト基盤の利用を本 issue に予定しているためである。

## 現状

- `test/test_zakuro.py` は `test_version(zakuro)` 1 個。`test/zakuro.py` は起動ラッパーとして
  `Zakuro` クラスを提供しているが、テストで利用されているのは 1 経路のみ
- C++ 単体テスト基盤は存在しない (CMakeLists.txt にテストターゲット・テストフレームワークなし)
- `.github/workflows/build.yml` はビルドとパッケージのみで pytest を実行しない
  (CI への pytest 組み込みは issues/0035 が担当)

## 設計方針

以下のテストを段階的に追加する (優先度順)。(E2E) と付すものは pytest + test/zakuro.py のラッパーで、
(C++ 単体) と付すものは本 issue で導入する単体テスト基盤 (GoogleTest / doctest / Catch2 のいずれか) を使う。

1. (E2E) **DataChannel シグナリング + 切断のクラッシュ回帰テスト** (issue 0002 の完了条件と噛み合う)
   - `--sora-data-channel-signaling` + `--duration 5 --repeat-interval 1 --vcs 2` を数分回した後
     SIGINT で終了させ、セグフォや異常終了がないことを確認する
   - `--repeat-interval 1` の場合、`src/zakuro.cpp` の `add_reconnect_scenario` は Exit を積まずに
     切断後に再接続を繰り返すため、プロセスは自然終了しない。数分実行後に SIGINT を送る
   - 本テストは issue 0002 (Sora C++ SDK 2026.2.1 への更新) の完了後に green になる前提 (現行の
     `2026.2.0-canary.19` ではクラッシュする)。数分規模の実行には pytest-timeout 等を使い、
     issues/0033 で削除・整理予定のため必要なら本 issue で再導入する
2. (C++ 単体) **ADM ライフサイクルのストレステスト** (issue 0003 / 0004 の回帰防止)
   - `Create → Init → Init（2 回目）→ RegisterAudioCallback → StartRecording → StopRecording →
     Terminate → 破棄` を 1000 回以上ループする
   - シーケンス・回数は issue 0004 の完了条件と揃え、0004 の検証と重複させず 1 つのテストで満たす
3. (C++ 単体) **VirtualClient::Close の 2 段階呼び出し状態遷移テスト** (issue 0024)
4. (C++ 単体 / E2E) **JSON-RPC エラーコード網羅テスト**
   - `-32600` / `-32601` / `-32603` と Notification: `JsonRpcHandler::Process` を直接呼ぶ単体テスト
   - `-32700` (Parse error): `JsonRpcHandler` では生成されず、
     `src/http_server.cpp` の `HttpSession::HandleJsonRpcRequest` (private) が
     `boost::json::parse` に失敗したときに生成される。pytest の E2E で不正 JSON を `/rpc` へ
     POST して検証する
5. (C++ 単体) **WavReader / Y4MReader の不正データ・境界値テスト** (issue 0021 / 0022 の回帰防止)
6. (C++ 単体) **`--config` JSONC の複数インスタンス設定テスト** (`Util::ParseInstanceToArgs` の網羅)

C++ 単体テスト基盤については以下を定める。

- GoogleTest / doctest / Catch2 のいずれかを導入する (選定は実装時に決定してよい)
- テストターゲットを CMakeLists.txt に追加し、CTest から実行できるようにする
- CI では build.yml に CTest 実行ステップを追加する (pytest の CI 組み込みは issues/0035 が担当)

本 issue の対象外: シナリオ切替 (`--scenario`)、`--vcs` 100 超の高負荷、SIGINT ハンドリング自体の
仕様検証 (SIGINT による終了経路は項目 1 で通る)。

## 完了条件

- 最低でも上記 1, 2, 4 のテストが CI で回っていること
  - 1 は実 Sora 接続が必要なため `sora_config` フィクスチャで gate され、環境変数が無い場合は skip される
  - 2 は C++ 単体テストとして CTest、4 は C++ 単体 (CTest) と pytest E2E の両方で実行される
- 実 Sora 接続が必要なテストは `sora_config` フィクスチャ
  (`TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY` の環境変数) で gate されていること
- 実 Sora 不要なテスト (単体、JsonRpcHandler、WAV/Y4M、config) は環境変数無しで実行できること
- C++ 単体テスト基盤が導入され、CTest でテストを実行できること
