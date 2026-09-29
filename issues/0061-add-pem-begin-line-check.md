# クライアント証明書と秘密鍵の PEM 開始行を行頭一致で確認する

- Created: 2026-09-26
- Completed: {YYYY-MM-DD}
- Branch: feature/add-pem-begin-line-check
- Polished: {YYYY-MM-DD}

## 目的

`--client-cert` / `--client-key` に指定したファイルの PEM 開始行を行頭一致で確認し、BOM 付きや行頭以外に `-----BEGIN` を含むファイルをエラーとして検出する。

現在の検証は部分一致のため、これらのファイルは検証を通過する。しかし Sora C++ SDK (BoringSSL) は PEM ヘッダを行頭一致でしか認識しないため、SDK 側で読み込みに失敗してクライアント証明書なしで接続を試みる。既定のログレベルでは警告も見えないため、mTLS 接続の失敗原因が分かりにくい。

## 現状

- `src/zakuro.cpp` の `IsPemCertificate` / `IsPemPrivateKey` は `std::string::find` による部分一致で、PEM の開始行が行頭にあるかを確認していない
- UTF-8 BOM 付きの証明書や、行の途中に `-----BEGIN CERTIFICATE-----` を含むだけのファイルは検証を通過する
- BoringSSL の PEM 読み込みは行頭一致 (`strncmp(buf, "-----BEGIN ", 11) == 0`) のため、SDK 側で `client_cert is set, but use_certificate_chain failed` の警告が出てクライアント証明書なしで接続を試みる

## 設計方針

- `IsPemCertificate` / `IsPemPrivateKey` の判定を「行頭に PEM の開始行がある」ことに変更する
- 開始行の位置がファイル先頭、または直前が改行 (`\n`) であることを確認する (`\r\n` の場合は `\n` の直後になるため同じ判定で扱える)
- `--client-key` は `-----BEGIN ` で始まる行に `PRIVATE KEY-----` が続くことを確認する
- 既存のエラーメッセージと `return 1` の扱いは変えない
- `test/test_client_cert.py` に BOM 付き PEM と行頭以外の `-----BEGIN` のケースを追加する

## 完了条件

- BOM 付き PEM、行頭以外に `-----BEGIN` を含むファイルを指定した場合、エラーメッセージが出力され、そのインスタンスが接続しないこと
- 通常の PEM (ファイル先頭または改行の直後に開始行がある) は従来どおり動作すること
- 追加した pytest のテストが pass すること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
