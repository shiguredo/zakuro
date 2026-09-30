# --vcs 100 規模の負荷でファイルディスクリプタが枯渇する

- Created: 2026-09-30
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-file-descriptor-exhaustion
- Polished: {YYYY-MM-DD}

## 目的

`--vcs` を大きくした負荷試験で、Zakuro 自身が消費するファイルディスクリプタ (FD) が枯渇し、
一部の仮想クライアントが接続に失敗する。枯渇時は sora-cpp-sdk の接続処理が例外で停止し、
プロセスが SIGABRT で終了する。

`src/main.cpp` の `EnsureFileDescriptorLimit` は枯渇を防ぐために追加された仕組みだが、
1 VC あたりの消費数の見積もり (`kFileDescriptorsPerVirtualClient` = 5) が実測より小さく、
枯渇を防げていない。実際の消費数に基づく見積もりとチェックに改め、FD を枯渇させない。

## 現状

`src/main.cpp` の `kFileDescriptorsPerVirtualClient` は 1 VC あたり 5 と見積もり、
`main` は全インスタンスの `--vcs` の合計 × 5 を必要数として `EnsureFileDescriptorLimit` に渡す。
soft limit が必要数以上であれば `EnsureFileDescriptorLimit` は何もせず起動する。

実測では 1 VC あたり約 10 の FD を消費する。

- 検証環境: Ubuntu 26.04 x86_64 (8 vCPU)、Sora C++ SDK 2026.2.2、libwebrtc m150、
  検証用 Sora へ TURN relay 経由で接続
- `--vcs 100` 実行時のピーク FD 数は 1006 / 1024 (soft limit 1024)
- `--vcs 100` の実行を繰り返すと 10 回に 1 回程度、`UDP socket creation failed` と接続失敗が発生する
- FD 枯渇時は sora-cpp-sdk 2026.2.2 の `SoraSignaling::DoConnect` が `std::random_device` の生成に
  失敗して `std::system_error` を投げ、未捕捉例外により SIGABRT (exit code 134) でプロセスが終了する
  (この終了経路そのものの対処は本 issue の対象外)
- soft limit 1024 の環境では必要数 500 (= 100 VC × 5) を満たしていると判定されるため、
  枯渇の危険を検知できない

`test/test_main_resource.py` の `test_file_descriptor_limit_is_rejected` /
`test_file_descriptor_limit_is_raised` / `test_file_descriptor_limit_sums_all_instances` は
現在の見積もり (5 × vcs) を前提にしている。

## 再現手順

1. soft limit を Zakuro の必要数 (100 VC × 5 = 500) ちょうどに下げる (`ulimit -n 500`)。
   この値では `EnsureFileDescriptorLimit` は足りていると判定し、起動する
2. `--vcs 100` で検証用 Sora へ接続する
3. 接続開始から約 10 秒で FD が枯渇し、SIGABRT (exit code 134) で終了する
4. soft limit 1024 のままでも、同じ実行を繰り返すと 10 回に 1 回程度再現する

FD 数は `/proc/<pid>/fd` のエントリ数を 1 秒間隔で数えたピーク値で計測した。

## 設計方針

「FD を枯渇させない」ことを目的として、以下を検討する。

- 1 VC あたりの必要数の見積もりを実測に基づいて引き上げる
  - ICE / TURN / DTLS / WebSocket の構成、IPv4 / IPv6、relay の有無で消費数が変わるため、
    複数構成で計測したうえで決定する
- 1 VC あたりの FD 消費そのものを削減できるか確認する
  - sora-cpp-sdk / libwebrtc の設定 (ICE candidate の種類、socket の共有可否など) で
    削減できるか要調査とする
- 必要数を満たせない環境では起動せず、`FileDescriptorLimitMessage` の経路で明確なエラーを出す
  動作を維持する
- 再発防止として、FD 上限を絞った状態で `--vcs` を実行して枯渇しないことを検証するテストを
  `test/test_main_resource.py` に追加する

## 完了条件

- 1 VC あたりの FD 消費数の見積もりが実測に基づいて見直され、根拠が残っていること
- 見直した見積もりに基づき、大きい `--vcs` の負荷でも FD が枯渇しないこと
  (満たせない環境では起動時にエラーで止まること)
- `test/test_main_resource.py` の FD 上限テストが見直し後の値に追従し、CI が通ること

## pending にした理由

必要数の決定には TURN relay の有無やプラットフォーム差を踏まえた追加計測が必要であり、
見積もりを引き上げるか 1 VC あたりの消費を削減するかの設計判断も要する。
また、見積もりを引き上げると利用環境によっては起動時に要求する FD 数が増えるため、
影響を確認してから対応する必要があるため pending とする。
