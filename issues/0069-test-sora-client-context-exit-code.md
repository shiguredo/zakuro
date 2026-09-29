# SoraClientContext の生成失敗でプロセスが非ゼロ終了することをテストで確認する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-sora-client-context-exit-code-test
- Polished: {YYYY-MM-DD}

## 目的

`SoraClientContext::Create` が nullptr を返した場合に、クラッシュせずに接続を中止するだけでなく、
プロセスが非ゼロで終了することをテストで保証する。

従来は `Zakuro::Run` の戻り値を `main` が捨てていたため、この経路の終了コードは 0 のままで、
テストからは「クラッシュしていないこと」しか観測できなかった。`Zakuro::Run` の戻り値を `main` が
終了コードへ反映する変更 (issues/0031) が入り、この経路の終了コードは 1 になった。
終了コードの検査を追加しないと、`Zakuro::Run` の戻り値が再び終了コードへ反映されなくなっても
テストが素通りする。

## 現状

`test/test_sora_client_context.py` の次の 2 つのテストは、シグナルによる強制終了 (負の終了コード)
でないことしか検査していない。

- `test_context_failure_does_not_crash`: 設定ファイル経由で生成を失敗させる
- `test_context_failure_cli_exits_without_signal`: コマンドライン経由で生成を失敗させる

いずれも `assert result.returncode >= 0` のみで、終了コード 1 を検査していない。
`test_context_failure_does_not_crash` の docstring には「`Zakuro::Run` の戻り値は `main` が
捨てているため、終了コードには反映されない」という、現状と一致しなくなった記述が残っている。

実測では、どちらの経路も `failed to create Sora client context` を標準エラー出力に出して
終了コード 1 で終了する。`Zakuro::Run` は `VirtualClient` の生成前に `config_.context` を検査し、
nullptr の場合は 1 を返す。`main` は 0 以外の戻り値をすべてエラーとして 1 に集約する。

生成を失敗させるには `--h264-encoder cisco_openh264` と `--openh264 /dev/null` を組み合わせる。
OpenH264 ライブラリではないファイルを指定すると `cisco_openh264` エンジンが能力から消え、
プリファレンスの検証に失敗して `SoraClientContext::Create` が nullptr を返す。

## 設計方針

- 2 つのテストの assertion を `assert result.returncode == 1` に変更する。この経路で 0 になるのは
  シグナルでも 0 でも異常であり、非負であることの確認では退行を検出できない
- `test_context_failure_does_not_crash` の docstring から「終了コードには反映されない」という
  記述を削除し、非ゼロで終了することの確認に置き換える
- シグナルによる強制終了の検出は終了コード 1 の検査に含まれるため、`>= 0` の assertion は残さない
- 終了コードが 0 以外であればよいのか 1 であるべきかは、この経路の `Zakuro::Run` の戻り値が 1 に
  固定されているため 1 で検査する。`main` は 0 以外をすべて 1 に集約するため、将来 `Zakuro::Run` の
  戻り値が増えてもこの検査は変わらない

## 完了条件

- `test_context_failure_does_not_crash` が、設定ファイル経由で `SoraClientContext` の生成に失敗した
  ときに終了コード 1 で終了することを検査していること
- `test_context_failure_cli_exits_without_signal` が、コマンドライン経由で生成に失敗したときに
  終了コード 1 で終了することを検査していること
- `Zakuro::Run` の戻り値を `main` が終了コードへ反映しなくなった場合に、この 2 つのテストが
  失敗すること (終了コードの検査を実際に外して確認する)
- 既存の pytest が全て通ること

## 解決方法

実装時に記入する。
