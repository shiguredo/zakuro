# CLI と設定ファイルで `--client-cert` / `--client-key` を片方ずつ指定した場合に警告する

- Created: 2026-09-26
- Completed: {YYYY-MM-DD}
- Branch: feature/add-client-cert-key-mixed-source-warning
- Polished: {YYYY-MM-DD}

## 目的

コマンドラインと設定ファイルのそれぞれで `--client-cert` / `--client-key` の片方だけが指定された場合、最終的な証明書と秘密鍵が別のソースから組み合わさることを警告する。

片方だけの指定は `src/util.cpp` の `Util::ParseArgs` で検出してエラーにしているが、CLI と設定ファイルをまたぐと最終値が両方非空になり検証を通過する。証明書と秘密鍵が一致しないファイルの組み合わせのまま起動し、TLS ハンドシェイクの失敗まで気付けない。

## 現状

- `src/main.cpp` の設定ファイル処理は、`--config` 以外のコマンドライン引数 (`post_args`) を各インスタンスの引数の末尾に連結する
- `src/util.cpp` の `Util::ParseArgs` は `app.option_defaults()->take_last()` を使っており、後から並ぶコマンドライン側の値が優先される
- 設定ファイルのインスタンスに `client-key` のみ、コマンドラインに `--client-cert` のみを指定すると、最終的な `client_cert` / `client_key` は両方非空になり、ペア検証を通過して接続を試みる
- 設定ファイルのインスタンスに両方を指定し、コマンドラインで片方だけを上書きした場合も、証明書と秘密鍵が別のソースの組み合わせになる

## 設計方針

- `src/main.cpp` の設定ファイル処理で、コマンドライン引数に `--client-cert` / `--client-key` の片方だけがあり、かつインスタンス設定に他方がある場合に `std::cerr` に警告を出力する
- 警告は英語にし、ペアが別のソースから構成されることを伝える (例: `client cert and client key are specified in different places (command line and config file)`)
- 既存の上書き挙動は維持し、エラーにはしない
- 設定ファイルのみ・コマンドラインのみ・同じソースで完結する場合は警告しない
- `test/zakuro.py` の `Zakuro` に追加のコマンドライン引数を渡せる引数を追加し、pytest で警告を検証する

## 完了条件

- 設定ファイルに片方、コマンドラインに他方を指定した場合に警告が stderr に出力され、起動は継続すること
- 設定ファイルに両方を指定し、コマンドラインで片方を指定した場合も警告が出力されること
- 設定ファイルのみ・コマンドラインのみ・コマンドラインで両方を指定した場合は警告が出力されないこと
- 追加した pytest のテストが pass すること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
