# `--client-cert` / `--client-key` に PEM として不正なファイルを指定した場合はエラーにする

- Created: 2026-09-26
- Completed: {YYYY-MM-DD}
- Branch: feature/add-client-cert-pem-validation
- Polished: {YYYY-MM-DD}

## 目的

クライアント証明書・秘密鍵のファイルが PEM として解釈できない場合に、Sora C++ SDK へ渡す前にエラーとして検出する。

現状は内容が空かどうかしか検査しておらず、空白のみのファイルや DER 形式・壊れた PEM はそのまま SDK に渡る。SDK は警告を出してクライアント証明書なしで接続を試みるため、mTLS 必須のサーバーではハンドシェイクに失敗し、警告も既定のログレベルでは見えないため原因が分かりにくい。

## 現状

- `src/util.cpp` の `Util::LoadFileContents` はファイルを読み込むだけで、PEM としての妥当性は検証しない
- `src/zakuro.cpp` の `Zakuro::Run` は `load_pem_file` ラムダで「ファイルを開けない場合」と「内容が空の場合」だけをエラーにしている
- 空白のみのファイルや PEM の開始行を含まないファイル (DER 形式、バイナリ、壊れたテキスト) は `SoraSignalingConfig::client_cert` / `client_key` に設定される
- Sora C++ SDK の `Websocket::CreateSSLContext` は `use_certificate_chain` / `use_private_key` の失敗を `client_cert is set, but use_certificate_chain failed` などの `RTC_LOG(LS_WARNING)` で出力し、クライアント証明書なしで接続を続行する
- 警告は `--log-level warning` 以上を指定しないと stderr に出ず、既定の `--log-level none` では `webrtc_logs.*` にのみ記録される

## 設計方針

- `src/zakuro.cpp` の `Zakuro::Run` にある `load_pem_file` ラムダで内容を検査し、PEM の開始行を含まない場合はエラーにする
- `--client-cert` は `-----BEGIN CERTIFICATE-----` を含むこと、`--client-key` は `-----BEGIN ` と `PRIVATE KEY-----` を含むことを確認する
- 暗号化された秘密鍵 (`ENCRYPTED PRIVATE KEY`) や証明書チェーン、`RSA PRIVATE KEY` / `EC PRIVATE KEY` / `PRIVATE KEY` を弾かないこと
- 厳密な PEM パースは行わず、SDK に渡す前の明らかな誤りを弾くことに留める
- 不正な場合は `std::cerr` にエラーを出力してそのインスタンスを `return 1` で終了する (既存の読み込み失敗と同じ扱い)
- エラーメッセージは英語にする (例: `client cert is not PEM format: <path>`)

## 完了条件

- 空白のみのファイルや PEM の開始行を含まないファイルを `--client-cert` / `--client-key` に指定した場合、エラーメッセージが出力され、そのインスタンスが接続しないこと
- 通常の PEM (証明書チェーン、`RSA PRIVATE KEY` / `EC PRIVATE KEY` / `PRIVATE KEY` / `ENCRYPTED PRIVATE KEY`) は従来どおり動作すること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
- 追加した pytest のテストが pass すること
