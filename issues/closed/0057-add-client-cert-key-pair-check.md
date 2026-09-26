# `--client-cert` と `--client-key` の片方だけを指定した場合はエラーにする

- Created: 2026-09-26
- Completed: 2026-09-26
- Branch: feature/add-client-cert-key-pair-check
- Polished: 2026-09-26

## 目的

`--client-cert` と `--client-key` の片方だけを指定した設定ミスを起動時に検出し、エラーとして終了させる。

現状は片方だけでも受け付けられ、接続を試みてから TLS ハンドシェイクの失敗や SDK の警告でしか気付けない。警告は既定のログレベルでは stderr に出ないため、原因の特定に時間がかかる。

## 現状

- `src/util.cpp` の `--client-cert` / `--client-key` はそれぞれ独立に `CLI::ExistingFile` で検証されるだけで、組み合わせは検証していない
- `src/zakuro.cpp` の `Zakuro::Run` は `config_.client_cert` / `client_key` が空でない場合にそれぞれ PEM ファイルを読み込んで `SoraSignalingConfig` に設定する。片方だけでも設定される
- Sora C++ SDK の `SoraSignaling::CreatePeerConnection` は、片方だけが設定されている場合に `TURN-TLS client certificate requires both client_cert and client_key` を `RTC_LOG(LS_WARNING)` で出力する
- WSS でもクライアント証明書と秘密鍵が揃わないと mTLS のクライアント証明書認証は成立しない
- 警告は `--log-level warning` 以上を指定しないと stderr に出ず、既定の `--log-level none` では `webrtc_logs.*` にのみ記録される

## 設計方針

- `Util::ParseArgs` の必須オプション検証と同じ場所で、`client_cert` / `client_key` の一方のみが設定されている場合にエラーにする
- エラーメッセージは既存の `--sora-signaling-url is required` と同じ形式で英語にする (例: `--client-cert and --client-key must be specified together`)
- 設定ファイル経由のインスタンスも `Util::ParseInstanceToArgs` から `Util::ParseArgs` を通るため、同じ検証がかかる
- 両方指定・両方未指定は従来どおり許可する
- `Util::ParseArgs` のシグネチャとエラー時の終了方法は issues/0031 で変更が予定されているため、実装時点の構造 (戻り値でエラーを返す方式か `std::exit` か) に合わせる

## 完了条件

- `--client-cert` のみ、または `--client-key` のみを指定した場合にエラーメッセージが出力され、非 0 で終了すること
- `--client-cert` と `--client-key` の両方を指定した場合、および両方未指定の場合は従来どおり動作すること
- 設定ファイル経由のインスタンスでも同じ検証がかかること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
- 追加した pytest のテストが pass すること

## 解決方法

`src/util.cpp` の `Util::ParseArgs` に、`client_cert` / `client_key` の一方のみが設定されている場合にエラーにする検証を追加した。既存の必須オプション検証と同じ位置・同じ形式 (`std::cerr` + `std::exit(1)`) で、`--client-cert and --client-key must be specified together` を出力する。設定ファイル経由のインスタンスも `Util::ParseInstanceToArgs` から `Util::ParseArgs` を通るため、同じ検証がかかる。

- `test/test_client_cert.py` に、設定ファイル経由とコマンドライン経由で片方だけを指定した場合のテストを追加した (終了コード 1 とエラーメッセージを確認)
- テストから CLI を直接起動できるように、`test/zakuro.py` の `Zakuro._get_zakuro_executable_path` をモジュール直下の `get_zakuro_executable_path` に移動した
- `CHANGES.md` の `## develop` に `[ADD]` を追記した

macOS arm64 で `python3 run.py build macos_arm64` が成功し、既存テストを含む pytest が pass することを確認した。
