# `--client-cert` / `--client-key` を指定しても mTLS 接続できない

- Created: 2026-09-25
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-client-cert-key-file-content
- Polished: {YYYY-MM-DD}

## 目的

クライアント証明書認証 (mTLS) が必須の Sora サーバーへ `--client-cert` / `--client-key` で接続できるようにする。

Sora C++ SDK 2025.1.0 で `SoraSignalingConfig::client_cert` / `client_key` に設定する値の意味が「PEM ファイルのパス」から「PEM ファイルの内容」に変更され、型も `std::string` から `std::optional<std::string>` に変更された。
zakuro はこの変更に追従しておらず、パス文字列をそのまま SDK へ渡しているため、クライアント証明書が読み込まれず TLS ハンドシェイクに失敗する。

## 現状

- `src/util.cpp` の `--client-cert` / `--client-key` オプションは `CLI::ExistingFile` でファイルパスを受け取り、`ZakuroConfig` (`src/zakuro.h`) の `client_cert` / `client_key` (`std::string`) に保持する
- `src/zakuro.cpp` の `Zakuro::Run` は `sora_config.client_cert = config_.client_cert;` と代入するだけで、ファイルを読み込んでいない
- Sora C++ SDK の `SoraSignalingConfig::client_cert` / `client_key` は `std::optional<std::string>` であり、SDK の `CreateSSLContext` は値の中身を PEM データとして `use_certificate` / `use_private_key` に渡す

このため PEM ファイルのパスが PEM データとして解釈され、以下の警告が出てクライアント証明書なしでハンドシェイクし、mTLS 必須のサーバーから拒否される。

```
client_cert is set, but use_certificate failed: NO_START_LINE (PEM routines, OPENSSL_internal)
client_key is set, but use_private_key failed: NO_START_LINE (PEM routines, OPENSSL_internal)
Failed Websocket handshake: last_ec=TLSV1_ALERT_CERTIFICATE_REQUIRED (SSL routines, OPENSSL_internal)
```

再現手順:

1. mTLS 必須の Sora サーバーを用意する
2. PEM 形式のクライアント証明書と秘密鍵を用意する
3. `--client-cert <cert.pem> --client-key <key.pem>` を付けて zakuro を起動する

期待: クライアント証明書が送信されて接続できる
実際: `use_certificate failed` / `use_private_key failed` の警告が出て `TLSV1_ALERT_CERTIFICATE_REQUIRED` で切断される

また、`--client-cert` / `--client-key` を未指定にした場合も、空の `std::string` が `std::optional<std::string>` に代入されて optional が engaged になる。SDK は証明書が設定済みとして扱い、空データの読み込みを試みる。

## 設計方針

Sora C++ SDK の sumomo (`examples/sumomo/src/sumomo.cpp`) と同じ方式にする。sumomo はパスが空でない場合のみファイル内容を読み込み、`SoraSignalingConfig::client_cert` / `client_key` に設定している。

- `src/zakuro.cpp` の `Zakuro::Run` で、`config_.client_cert` / `client_key` が空でない場合にファイルを読み込み、その内容を `sora_config.client_cert` / `client_key` に設定する
- 空の場合は代入しない (`std::nullopt` のままにする)。SDK 側で証明書が設定済みとして扱われることを防ぐ
- ファイルの読み込みに失敗した場合、または内容が空の場合は、証明書なしで接続せずエラーとして扱う
- ファイル読み込みは `std::ifstream` で行い、`Util::LoadJsoncFile` と同様に実装する
- `CHANGES.md` の `## develop` に `[FIX]` を追記する

## 完了条件

- `--client-cert` / `--client-key` に PEM ファイルを指定し、mTLS 必須の Sora サーバーへ接続できること
- 接続時に `use_certificate failed` / `use_private_key failed` の警告が出ないこと
- `--client-cert` / `--client-key` 未指定時に `SoraSignalingConfig::client_cert` / `client_key` が設定されないこと
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
- `test/test_zakuro.py` の既存テストが pass すること
- `CHANGES.md` に `[FIX]` が追記されていること

## 解決方法

未着手