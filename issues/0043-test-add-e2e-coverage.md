# E2E テストの拡充 (test_version 1 個からの脱却)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/add-e2e-coverage
- Polished: {YYYY-MM-DD}

## 目的

`test/test_zakuro.py` の実テストが `test_version` の 1 本のみで、
JSON-RPC の GetVersion を叩いてバージョンを検証するスモークテスト相当しかない。
Zakuro の主要責務である「N VC を張って負荷をかける」経路、DataChannel 切断、SIGINT ハンドリング、
`--vcs` 大量、`--duration` + `--repeat-interval`、`--config` JSONC、シナリオ切替などが全て未カバー。
CHANGES.md `misc` の `[ADD] pytest を使った E2E テストを追加する` の実態を強化する。

## 現状

`test/test_zakuro.py` は `test_version(zakuro)` 1 個。`test/zakuro.py` は起動ラッパーとして
`Zakuro` クラスを提供しているが、テストで利用されているのは 1 経路のみ。

## 設計方針

以下のテストを段階的に追加する (優先度順)。

1. **DataChannel シグナリング + 切断のクラッシュ回帰テスト** (issue 0002 の完了条件と噛み合う)
   - `--sora-data-channel-signaling` + `--duration 5 --repeat-interval 1 --vcs 2` を数分回し、
     セグフォせず正常終了することを確認
2. **ADM ライフサイクルのストレステスト** (issue 0003 / 0004 の回帰防止)
   - Create → Init → StartRecording → StopRecording → Terminate → 破棄 を単体テストレベルで数百回ループ
3. **VirtualClient::Close の 2 段階呼び出し状態遷移テスト** (issue 0024)
4. **JsonRpcHandler のエラーコード網羅テスト** (`-32700` / `-32600` / `-32601` / `-32603` および Notification)
5. **WavReader / Y4MReader の不正データ・境界値テスト** (issue 0021 / 0022 の回帰防止)
6. **`--config` JSONC の複数インスタンス設定テスト** (Util::ParseInstanceToArgs の網羅)

C++ 単体テストは GoogleTest / doctest / Catch2 のいずれかを導入する必要がある。
E2E テストは pytest + test/zakuro.py のラッパーで書ける。

## 完了条件

- 最低でも上記 1, 2, 4 のテストが CI で回っていること
- 実 Sora 接続が必要なテストは `TEST_SIGNALING_URLS` フィクスチャで gate されていること
- 実 Sora 不要なテスト (単体、JsonRpcHandler、WAV/Y4M) は環境変数無しで実行できること
