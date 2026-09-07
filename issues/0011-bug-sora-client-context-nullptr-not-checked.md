# sora::SoraClientContext::Create の nullptr 返却を無検査で使用してクラッシュする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
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
