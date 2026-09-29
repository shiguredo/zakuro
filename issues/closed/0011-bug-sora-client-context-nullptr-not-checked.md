# sora::SoraClientContext::Create の nullptr 返却を無検査で使用してクラッシュする

- Created: 2026-08-27
- Completed: 2026-09-29
- Branch: feature/fix-sora-client-context-nullptr-not-checked
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`sora::SoraClientContext::Create` は環境依存の経路で nullptr を返すが、
`Zakuro::Run` はこれを検査せずに `VirtualClient::Connect` へ渡し、
`config_.context` の nullptr dereference によりクラッシュする経路を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` は
`vc_config.context = sora::SoraClientContext::Create(context_config);` を実行し、
戻り値が nullptr かどうかを検査せずに `vc_configs` へ流し込み、
`VirtualClient::Create` に渡す。

`src/virtual_client.cpp` の `VirtualClient::Connect` は `config_.context` を
複数箇所で無条件に dereference する。
`config_.context->peer_connection_factory()` は CreateAudioTrack
(`config_.audio_type` が `NoAudio` 以外の場合) と CreateVideoTrack
(`config_.no_video_device` が false の場合) で使い、そのほかに
`config.pc_factory` への設定と `signaling_thread()` / `connection_context()` 経由の
network / socket factory の取得で使っているため、context が nullptr の場合は SIGSEGV する。

`sora::SoraClientContext::Create` は Sora C++ SDK の
`src/sora_client_context.cpp` で次の経路から nullptr を返す
(確認したのは SDK `2026.2.0-canary.19`。issue 0002 で `2026.2.1` への更新が予定されている)。

- `CreateVideoCodecFactory` が失敗した場合
  - ビデオコーデックのプリファレンス検証 (`ValidateVideoCodecPreference`) の失敗が含まれる
- `configure_dependencies` の実行後に ADM が nullptr になった場合
- PeerConnectionFactory の生成に失敗した場合
- ADM の初期化やオーディオデバイス設定に失敗した場合 (Android / iOS 以外)

Zakuro の構成で現実的に発生しやすいのはビデオコーデックのプリファレンス検証の失敗である。
利用できないビデオコーデック実装を明示指定したときに発生する。
例えば `--h264-encoder cisco_openh264` を指定した上で、
`--openh264` に存在するが OpenH264 ライブラリではないファイル (例: `/dev/null`) を
指定した場合、capability から kCiscoOpenH264 エンジンが消えるため
プリファレンス検証が失敗し、`SoraClientContext::Create` は nullptr を返す。

なお `--openh264` は CLI11 の `CLI::ExistingFile` チェック
(`src/util.cpp` の `--openh264` オプション定義) により、
存在しないパスは起動時の引数パースで拒否される。
そのため再現には「存在するが OpenH264 ではないファイル」を指定する必要がある。

## 設計方針

`Zakuro::Run` の `sora::SoraClientContext::Create` 呼び出し直後に nullptr を検査し、
エラーメッセージを出力して `return 1;` する。

エラーメッセージは英語で出力する (AGENTS.md の規約)。
`Zakuro::Run` の既存のエラー処理は `std::cerr` へ英語メッセージを出力する流儀
(capturer 生成失敗、DataChannel パース失敗など) なので、それに合わせる。

`Zakuro::Run` の戻り値を `main` が終了コードへ反映する変更は
issue 0031 (main.cpp のリソース管理) で扱う。
本 issue では `Zakuro::Run` が 0 以外を返すことまでを保証する。
プロセスの非ゼロ終了の確認は issue 0031 の実装後になる点に注意する。

`VirtualClient::Create` 側でも defensive に `assert(config.context)` を入れておくと、
テストや将来の別呼び出しからのミスにも気付ける。
assert は NDEBUG ビルドでは無効になるため、デバッグビルドでの検出を目的とする。

## 完了条件

- `SoraClientContext::Create` が nullptr を返すシナリオ
  (例: `--h264-encoder cisco_openh264 --openh264 /dev/null` を指定) で
  クラッシュせず、`Zakuro::Run` が明確なエラーメッセージを出力して 0 以外を返すこと
- 正常経路には影響しないこと

## 解決方法

`SoraClientContext::Create` の戻り値を検査し、nullptr の場合はそのインスタンスの接続を
中止するようにした。

### `Zakuro::Run` の nullptr 検査

- `src/zakuro.cpp` の `Zakuro::Run` で `sora::SoraClientContext::Create` の直後に
  nullptr を検査し、`[<name>] failed to create Sora client context` を `std::cerr` へ
  出力して `return 1` するようにした。既存のエラーパス (capturer 生成失敗、fake audio の
  読み込み失敗、signaling URL の検証、PEM の読み込み失敗) と同じ書式と戻り値に揃えている
- 検査地点は io_context を回す前なので、生成に失敗したインスタンスは接続処理に到達しない。
  同じ `ZakuroConfig` を使う他のインスタンスは独立したスレッドで動き続ける

### `VirtualClient::Create` の defensive な assert

- `src/virtual_client.cpp` の `VirtualClient::Create` に `assert(config.context != nullptr)` を
  追加した。`VirtualClient` のコンストラクタは private であるため、この検査が呼び出し側の
  渡し漏れに気付く唯一の入口になる
- `assert` は NDEBUG ビルドでは無効であり、CI とリリースで使う Release ビルド
  (`-O3 -DNDEBUG`) では実際の防御にならない。防御は `Zakuro::Run` 側の検査が担い、
  assert はデバッグビルドでの診断に限られることをコメントに明記した

### テスト

- `test/test_sora_client_context.py` を追加した。次の 3 件で構成する
  - `test_context_failure_does_not_crash`: 設定ファイル経由で生成に失敗させ、シグナルで
    強制終了しないこと、エラーメッセージが出力されること、接続先へ到達しないことを確認する
  - `test_context_failure_cli_exits_without_signal`: コマンドライン経由でも同じことを確認する
  - `test_context_creation_succeeds_on_normal_path`: コーデック実装を明示指定しない場合は
    生成に成功し、接続処理が接続先まで到達することを確認する
- 失敗の再現は `--h264-encoder cisco_openh264` と `--openh264 /dev/null` の組み合わせで
  行う。OpenH264 ライブラリではないファイルを指定すると cisco_openh264 エンジンが能力から
  消え、プリファレンスの検証に失敗して `SoraClientContext::Create` が nullptr を返す
- テスト基盤の共通部分 (TLS ハンドシェイクだけを行うローカルサーバー、テスト用証明書の
  生成、ローカル接続用のインスタンス設定) は conftest へ集約し、`test/test_client_cert.py`
  と共有するようにした。これに伴い `test_client_cert.py` の重複実装を削除した

### 検証結果

- `python3 run.py build macos_arm64` でビルドが通り、`test/` 配下の pytest が
  67 passed / 1 skipped (skip は実 Sora 接続用の環境変数が無い既存テスト) になることを確認した
- 再現コマンドを実バイナリで実行し、修正前は SIGSEGV (終了コード 139) だったものが
  修正後はクラッシュせず `failed to create Sora client context` を出力することを確認した
- macOS では abort しないのに対し、Linux でも同じ経路になることは Sora C++ SDK 2026.2.1 の
  OpenH264 の能力判定が `_WIN32` 以外では `dlopen` を使う実装であることから確認した

### 残った課題

- `Zakuro::Run` の戻り値は `main` が捨てているため、プロセスの終了コードは 0 のままになる。
  実バイナリで終了コード 0 を確認した。終了コードを 1 にする変更は issue 0031 で扱う
- そのため完了条件の「0 以外を返す」は `Zakuro::Run` の戻り値としてのみ満たしており、
  テストで外部から観測することはできない。issue 0031 の実装後に終了コードの検査を追加する
