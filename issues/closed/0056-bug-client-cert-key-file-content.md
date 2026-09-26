# `--client-cert` / `--client-key` を指定しても mTLS 接続できない

- Created: 2026-09-25
- Completed: 2026-09-26
- Branch: feature/fix-client-cert-key-file-content
- Polished: 2026-09-26

## 目的

クライアント証明書認証 (mTLS) が必須の Sora サーバーへ `--client-cert` / `--client-key` で接続できるようにする。

Sora C++ SDK 2025.1.0 で `SoraSignalingConfig::client_cert` / `client_key` に設定する値の意味が「PEM ファイルのパス」から「PEM ファイルの内容」に変更され、型も `std::string` から `std::optional<std::string>` に変更された。
zakuro はこの変更に追従しておらず、パス文字列をそのまま SDK へ渡しているため、クライアント証明書が読み込まれず TLS ハンドシェイクに失敗する。

## 現状

- `src/util.cpp` の `--client-cert` / `--client-key` オプションは `CLI::ExistingFile` でファイルパスを受け取り、`ZakuroConfig` (`src/zakuro.h`) の `client_cert` / `client_key` (`std::string`) に保持する
- `src/zakuro.cpp` の `Zakuro::Run` は `sora_config.client_cert = config_.client_cert;` と代入するだけで、ファイルを読み込んでいない
- Sora C++ SDK の `SoraSignalingConfig::client_cert` / `client_key` は `std::optional<std::string>` であり、SDK の `CreateSSLContext` は値の中身を PEM データとして `use_certificate_chain` / `use_private_key` に渡す

このため PEM ファイルのパスが PEM データとして解釈され、以下のログが出てクライアント証明書なしでハンドシェイクし、mTLS 必須のサーバーから拒否される。

```
client_cert is set, but use_certificate failed: NO_START_LINE (PEM routines, OPENSSL_internal)
client_key is set, but use_private_key failed: NO_START_LINE (PEM routines, OPENSSL_internal)
Failed to Disconnect: message=Failed Websocket handshake: last_ec=TLSV1_ALERT_CERTIFICATE_REQUIRED (SSL routines, OPENSSL_internal) last_url=...
```

上のログは Sora C++ SDK 2025.6.0 をリンクしたビルドでの観測例であり、DEPS が指定する 2026.2.0-canary.19 では `use_certificate failed` が `use_certificate_chain failed` になる。これらは `RTC_LOG(LS_WARNING)` / `RTC_LOG(LS_ERROR)` で出力されるため、既定の `--log-level none` では stderr に出ず `webrtc_logs.*` にのみ記録される。`--log-level warning` 以上を指定すると stderr で確認できる。

再現手順:

1. mTLS 必須の Sora サーバーを用意する
2. PEM 形式のクライアント証明書と秘密鍵を用意する
3. `--client-cert <cert.pem> --client-key <key.pem>` を付けて zakuro を起動する (`--log-level warning` 以上を付けると警告が stderr に出力される)

期待: クライアント証明書が送信されて接続できる
実際: 証明書の読み込み失敗の警告 (`client_cert is set, but ... failed` / `client_key is set, but ... failed`) が出て `TLSV1_ALERT_CERTIFICATE_REQUIRED` で切断される

また、`--client-cert` / `--client-key` を未指定にした場合も、空の `std::string` が `std::optional<std::string>` に代入されて optional が engaged になる。SDK は証明書が設定済みとして扱い、空データの読み込みを試みる。

## 設計方針

Sora C++ SDK の sumomo (`examples/sumomo/src/sumomo.cpp`) の読み込み方式に倣う。sumomo はパスが空でない場合のみファイル内容を読み込み、`SoraSignalingConfig::client_cert` / `client_key` に設定している。ただし sumomo は読み込み失敗時に空文字列を設定するだけでエラーにしないため、エラー時の扱いは zakuro の方が厳しくする。

- `src/zakuro.cpp` の `Zakuro::Run` で、`config_.client_cert` / `client_key` が空でない場合にファイルを読み込み、その内容を `sora_config.client_cert` / `client_key` に設定する
- 空の場合は代入しない (`std::nullopt` のままにする)。SDK 側で証明書が設定済みとして扱われることを防ぐ
- ファイル読み込みは `Util::LoadJsoncFile` と同様に `std::ifstream` で行う。ただし `Util::LoadJsoncFile` は失敗時に例外を投げるのに対し、`Zakuro::Run` は `std::thread` 上で実行されるため未捕捉例外が `std::terminate` になる。例外は投げず、`src/zakuro.cpp` の既存パターンに合わせる
- ファイルのオープンに失敗した場合、または内容が空の場合は、`std::cerr` にエラーメッセージを出力してそのインスタンスの `Zakuro::Run` を `return 1` で終了する。`src/main.cpp` は `zakuro.Run()` の戻り値を捨てているため終了コードには反映されないが、stderr で判別できる
- pytest の E2E テストを追加する。openssl でテスト用 CA・サーバー証明書・クライアント証明書を一時生成し、クライアント証明書必須のローカル TLS サーバーへ zakuro 実バイナリを接続させ、サーバー側でクライアント証明書が送信されたことを確認する。`--client-cert` / `--client-key` / `--insecure` は設定ファイルのインスタンス設定 (`Util::ParseInstanceToArgs`) 経由で渡し、`test/zakuro.py` には stderr を取得する手段を追加する。テスト全体の拡充は issues/0043 が扱う
- `CHANGES.md` の `## develop` に `[FIX]` を追記する

## 完了条件

- `--client-cert` / `--client-key` に PEM ファイルを指定した場合、クライアント証明書が読み込まれて TLS ハンドシェイクで送信されること
  - 自動テストはクライアント証明書必須のローカル TLS サーバーで確認する
  - mTLS 必須の実 Sora サーバーへの接続確認は可能な環境で行う
- 接続時に `client_cert is set, but` / `client_key is set, but` で始まる警告が出力されないこと (`--log-level warning` 以上で確認)
- `--client-cert` / `--client-key` 未指定時に `SoraSignalingConfig::client_cert` / `client_key` が設定されないこと。SDK はこれらが設定されている場合に `client_cert is set` / `client_key is set` を出力するため、`--log-level info` 以上で接続してこれらが出力されないことで確認する
- ファイルのオープンに失敗した場合、または内容が空の場合は、stderr にエラーメッセージが出力され、そのインスタンスが接続しないこと
- 追加する pytest の E2E テストが pass すること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
- `test/test_zakuro.py` の既存テストが pass すること
- `CHANGES.md` に `[FIX]` が追記されていること

## 解決方法

`Zakuro::Run` で `config_.client_cert` / `client_key` のパスが空でない場合にファイルの内容を読み込み、`SoraSignalingConfig::client_cert` / `client_key` に設定するようにした。パスが空の場合は設定せず、`std::optional` が engaged にならないようにしている。

- `src/util.h` / `src/util.cpp` に `Util::LoadFileContents` を追加した。`std::ifstream` でファイル全体を読み込み、開けない場合や読み込みに失敗した場合は `std::nullopt` を返す
- `src/zakuro.cpp` の `Zakuro::Run` で PEM ファイルを読み込み、読み込み失敗・内容が空の場合は `std::cerr` にエラーを出力してそのインスタンスを `return 1` で終了する。例外は `std::thread` 上で `std::terminate` になるため投げない
- `src/zakuro.h` の `ZakuroConfig::client_cert` / `client_key` が PEM ファイルのパスであることをコメントで明記した
- `test/zakuro.py` に stderr をスレッドで読み続けて `stderr_output` で参照できる仕組みを追加した
- `test/test_client_cert.py` を追加し、openssl で生成したテスト用証明書とクライアント証明書必須のローカル TLS サーバーを使った E2E テストを実装した
  - クライアント証明書が TLS ハンドシェイクで送信されること
  - 未指定時に SDK に証明書が設定されないこと
  - 読み込み失敗 (空ファイル・読み取り権限なし) の場合はエラーになり接続しないこと
  - 読み込みに失敗したインスタンス以外は接続を継続すること
- `CHANGES.md` の `## develop` に `[FIX]` を追記した

macOS arm64 で `python3 run.py build macos_arm64` が成功し、`test/test_zakuro.py` と `test/test_client_cert.py` のテストが pass することを確認した。mTLS 必須の実 Sora サーバーへの接続確認は環境がないため行っていない。