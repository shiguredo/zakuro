# SoraClientContext の生成失敗でプロセスが非ゼロ終了することをテストで確認する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-sora-client-context-exit-code-test
- Polished: 2026-09-30

## 目的

`SoraClientContext::Create` が nullptr を返した場合に、クラッシュせずに接続を中止するだけでなく、
プロセスが終了コード 1 で終了することをテストで保証する。

従来は `Zakuro::Run` の戻り値を `main` が捨てていたため、この経路の終了コードは 0 のままで、
テストからは「クラッシュしていないこと」しか観測できなかった。`Zakuro::Run` の戻り値を `main` が
終了コードへ反映する変更 (issues/0031) が入り、この経路の終了コードは 1 になった。

この経路の終了コード 1 は現状テストされていない。`test/test_main_resource.py` の
`test_run_failure_exits_with_nonzero` は 0 バイトの WAV で `Zakuro::Run` を失敗させる経路を使い、
「`main` が `Zakuro::Run` の戻り値を終了コードへ反映する仕組み」を検証しているが、
この経路の `Zakuro::Run` が 1 を返すことまでは検証しない。現行の 2 テストは stderr の
エラーメッセージと非強制終了のみを検査しているため、仮にこの経路の null 検査が
「警告を出して続行する」などに変わって終了コード 0 になってもテストが素通りする。
この経路の終了コードを検査することで、この経路の `Zakuro::Run` の戻り値と `main` の反映の
両方を 1 つのテストで固定する。

なお、issues/closed/0011 の「追記 (2026-09-29)」では、終了コードの検査が
`test_run_failure_exits_with_nonzero` と重複するとして、この経路への終了コード検査の追加を
見送っている。0011 の追記が対象にしたのは「main の反映の仕組み」であり、ここで追加するのは
「この経路固有の `Zakuro::Run` の戻り値 1」なので検査内容は重複しない。

## 現状

`test/test_sora_client_context.py` の次の 2 つのテストは、シグナルによる強制終了 (負の終了コード)
でないことしか検査していない。

- `test_context_failure_does_not_crash`: 設定ファイル経由で生成を失敗させる
- `test_context_failure_cli_exits_without_signal`: コマンドライン経由で生成を失敗させる

いずれも `assert result.returncode >= 0` のみで、終了コード 1 を検査していない。
`test_context_failure_does_not_crash` の docstring には「`Zakuro::Run` の戻り値は `main` が
捨てているため、終了コードには反映されない」という、現状と一致しなくなった記述が残っている。

実測では、どちらの経路も `failed to create Sora client context` を標準エラー出力に出して
終了コード 1 で終了する (issues/closed/0011 の追記 2026-09-29 でも同じ実測を記録している)。
`Zakuro::Run` は `VirtualClient` の生成前に `VirtualClientConfig` のメンバー
`vc_config.context` を検査し、nullptr の場合は 1 を返す (src/zakuro.cpp の `Zakuro::Run`)。
`main` は 0 以外の戻り値をすべてエラーとして 1 に集約する。

生成を失敗させるには `--h264-encoder cisco_openh264` と `--openh264 /dev/null` を組み合わせる。
OpenH264 ライブラリではないファイルを指定すると `cisco_openh264` エンジンが能力から消え、
プリファレンスの検証に失敗して `SoraClientContext::Create` が nullptr を返す。

## 設計方針

- 2 つのテストの assertion を `assert result.returncode == 1` に変更する。この経路で終了コードが
  0 になるのは退行であり、シグナルによる強制終了 (負の終了コード) も異常である。
  非負であることの確認ではこの 2 つを見逃すため、退行を検出できない
- `test_context_failure_does_not_crash` の docstring から「終了コードには反映されない」という
  記述を削除し、終了コード 1 で終了することの確認に置き換える
- シグナルによる強制終了の検出は終了コード 1 の検査に含まれるため、`>= 0` の assertion は残さない
- 終了コードが 0 以外であればよいのか 1 であるべきかは、この経路の `Zakuro::Run` の戻り値が 1 に
  固定されているため 1 で検査する。`main` は 0 以外をすべて 1 に集約するため、将来 `Zakuro::Run` の
  戻り値が増えてもこの検査は変わらない

## 完了条件

- `test_context_failure_does_not_crash` が、設定ファイル経由で `SoraClientContext` の生成に失敗した
  ときに終了コード 1 で終了することを検査していること
- `test_context_failure_cli_exits_without_signal` が、コマンドライン経由で生成に失敗したときに
  終了コード 1 で終了することを検査していること
- この経路の `Zakuro::Run` が終了コード 1 を返さなくなった場合 (null 検査の削除や戻り値の変更)
  に、この 2 つのテストが失敗すること (終了コードの検査を実際に外して確認する)
- 既存の pytest が全て通ること

## 解決方法

実装時に記入する。
