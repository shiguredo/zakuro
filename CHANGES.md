# 変更履歴

- CHANGE
  - 下位互換のない変更
- UPDATE
  - 下位互換がある変更
- ADD
  - 下位互換がある追加
- FIX
  - バグ修正

## develop

- [ADD] signaling URL のバリデーションを追加
  - signaling URL が ws:// または wss:// で始まらない場合はエラーを出力
  - @voluntas
- [ADD] H.265 コーデックサポートを追加
  - `--sora-video-codec-type` に H265 を追加
  - `--sora-video-h265-params` オプションを追加
  - @voluntas
- [ADD] 各ビデオコーデックのパラメータ設定オプションを追加
  - `--sora-video-vp9-params` オプションを追加
  - `--sora-video-av1-params` オプションを追加
  - `--sora-video-h264-params` オプションを追加
  - @voluntas
- [ADD] コーデックプリファレンス設定オプションを追加
  - VP8, VP9, AV1, H.264, H.265 の各コーデックに対して、エンコーダーの実装を選択可能
  - `--vp8-encoder`
  - `--vp9-encoder`
  - `--av1-encoder`
  - `--h264-encoder`
  - `--h265-encoder`
  - 選択可能な実装: internal, cisco_openh264, intel_vpl, nvidia_video_codec_sdk, amd_amf
  - @voluntas
- [ADD] YAML 設定ファイルで全てのコーデックパラメータとプリファレンスに対応
  - @voluntas
- [ADD] `--show-video-codec-capability` オプションを追加
  - 利用可能なビデオコーデックのエンコーダーなどを出力
  - @voluntas

### misc

- [ADD] .github ディレクトリに copilot-instructions.md を追加
  - @torikizi


## 2025.1.0

**リリース日**: 2025-06-19

- [CHANGE] `--sora-dir`, `--sora-args` を `--local-sora-cpp-sdk-dir` と `--local-sora-cpp-sdk-args` に変更する
  - @melpon
- [CHANGE] `--webrtc-build-dir`, `--webrtc-build-args` を `--local-webrtc-build-dir` と `--local-webrtc-build-args` に変更する
  - @melpon
- [CHANGE] Ubuntu 20.04 のビルドを削除
  - @melpon
- [CHANGE] multistream のオプションを削除
  - @torikizi
- [CHANGE] FakeNetwork 系の機能を削除
  - @melpon
- [UPDATE] CMakeLists の依存から libva と libdrm を削除する
  - @zztkm
- [UPDATE] CI の Ubuntu で libva と libdrm をインストールしないようにする
- [UPDATE] Sora C++ SDK を `2025.3.1` に上げる
  - それに伴って以下のライブラリのバージョンも上げる
  - libwebrtc のバージョンを `m136.7103.0.0` に上げる
  - Boost のバージョンを `1.88.0` に上げる
  - CMake のバージョンを `4.0.1` に上げる
  - OpenH264 のバージョンを `2.6.0` に上げる
  - Blend2D のバージョンを `717cbf4bc0f2ca164cf2f0c48f0497779241b6c5` に上げる
  - AsmJit のバージョンを `e8c8e2e48a1a38154c8e8864eb3bc61db80a1e31` に上げる
  - `#include <rtc_base/helpers.h>` を `#include <rtc_base/crypto_random.h>` に置き換える
  - `boost::json::error_code` を `boost::system::error_code` に置き換える
  - `absl::nullopt` を `std::nullopt` に置き換える
  - `boost::optional` を `std::optional` に置き換える
  - `SoraVideoEncoderFactoryConfig` の `use_simulcast_adapter` を削除
  - `VideoCodecPreference` を利用して `use_hardware_encoder` を削除
  - 利用するエンコーダ/デコーダを `VideoCodecPreference` で指定する
  - `NopVideoDecoder` を `VideoCodecPreference` の仕組みに乗せる
  - @melpon @voluntas @zztkm @torikizi
- [UPDATE] Blend2D, AsmJit を最新版に上げる
  - @melpon @torikizi @voluntas
- [UPDATE] Lyra 用の設定を削除する
  - 2024.1.0 で機能廃止済であるため残った設定を削除
  - @miosakuma
- [UPDATE] YAML_CPP_VERSION を `2f86d13775d119edbb69af52e5f566fd65c6953b` にアップデート
  - yaml-cpp は `0.8.0` 以降リリースされておらず、新しいバージョンの CMAKE と互換性が失われている
  - リリースバージョン指定をやめ、GitHub の master ブランチのコミットを指定する
    - このコミットハッシュを指定した理由は以下の通り
      - 対応時点での最新のコミットであること
      - CMake 4.0.1 でビルドが通ることを確認できたこと
  - この対応は yaml-cpp のリリースが行われるまでの暫定的な対応で、最新のリリースが行われた場合はバージョン指定に戻す
  - @torikizi
- [ADD] Ubuntu 24.04 のビルドを追加
  - @melpon
- [ADD] `--degradation-preference` 引数を追加
  - @melpon
- [ADD] H.265 でも実際のデコード処理を行わず、固定サイズ（320x240）のダミーフレームを返すデコーダーを追加する
  - @voluntas
- [ADD] --sora-video-codec-type オプションに H265 を追加
  - 利用可能な映像コーデックに H.265 を追加
  - @voluntas
- [FIX] ビデオコーデックが未指定かつサイマルキャスト利用時に入力チェックエラーになる問題を修正する
  - ビデオコーデックが未指定でもサイマルキャストは利用可能であるため、チェックを削除
  - @miosakuma

### misc

- [CHANGE] VERSION ファイルを Zakuro のバージョンにのみ利用するようにする
  - @voluntas
- [CHANGE] GitHub Actions の ubuntu-latest を ubuntu-24.04 に変更
  - @voluntas
- [ADD] 依存ライブラリ用に DEPS ファイルを追加
  - @voluntas
- [ADD] canary.py を追加
  - @voluntas @torikizi
- [FIX] canary リリースの時は prerelease フラグをつける
  - @miosakuma

## 2024.1.0

**リリース日**: 2024-04-24

- [CHANGE] Lyra を削除
  - `--sora-audio-codec-lyra-bitrate` オプションを削除
  - `--sora-audio-codec-lyra-usedtx` オプションを削除
  - `--sora-check-lyra-version` オプションを削除
  - `--sora-audio-codec-type` から LYRA を削除
  - インストールするファイルに model_coeffs を含めないようにする
  - VERSION ファイルから LYRA_VERSION を削除
  - @melpon
- [ADD] run.py に `--webrtc-build-dir` と `--webrtc-build-args` オプションを追加
  - @melpon
- [ADD] run.py に `--sora-dir` と `--sora-args` オプションを追加
  - @melpon
- [UPDATE] Sora C++ SDK を 2024.6.1 に上げる
  - それに伴って以下のライブラリのバージョンも上げる
  - WebRTC を m122.6261.1.0 に上げる
    - Ubuntu のビルドを通すために、 __assertion_handler というファイルをコピーする処理を追加した
  - Boost を 1.84.0 に上げる
  - @torikizi @voluntas @melpon
- [UPDATE] asmjit と blend2d を最新版に上げる
  - @voluntas @melpon
- [UPDATE] `CMake` を `3.28.1` に上げる
  - @voluntas @torikizi @melpon
- [UPDATE] OpenH264 を 2.4.1 に上げる
  - @melpon
- [UPDATE] CLI11 を 2.4.2 に上げる
  - @melpon @torikizi
- [UPDATE] run.py に定義されていた関数を buildbase.py に移動する
  - @melpon
- [UPDATE] Github Actions の actions/checkout , actions/upload-artifact , actions/download-artifact をアップデート
  - Node.js 16 の Deprecated に伴うアップデート
    - actions/checkout@v3 から actions/checkout@v4 にアップデート
    - actions/upload-artifact@v3 から actions/upload-artifact@v4 にアップデート
    - actions/download-artifact@v3 から actions/download-artifact@v4 にアップデート
  - @miosakuma @torikizi
- [FIX] VideoCodec は Protected のため CreateVideoCodec に修正
  - m117.5938.2.0 へのアップデートに伴う修正
  - @torikizi
- [FIX] resetMatrix() から resetTransform() に修正
  - blend2d を最新版に上げた際に発生した問題の修正
  - @torikizi

## 2023.1.0 (2023-08-23)

- [CHANGE] `--fake-network-send-codel-active-queue-management` と `--fake-network-receive-codel-active-queue-management` オプションを削除
  - @melpon
- [CHANGE] YAML 設定の `instance_num` と `instance-num` に変更する
  - @melpon
- [UPDATE] WebRTC を m115.5790.7.0 に上げる
  - @melpon @torikizi
- [UPDATE] Sora C++ SDK を 2023.9.0 に上げる
  - @melpon @torikizi
- [UPDATE] `CMake` を `3.26.4` に上げる
  - @voluntas
- [UPDATE] `OpenH264` を `2.3.1` に上げる
  - @voluntas
- [UPDATE] `CLI11` を `2.3.2` に上げる
  - @voluntas
- [UPDATE] `Boost` を `1.82.0` に上げる
  - @melpon
- [UPDATE] actions/create-release と actions/upload-release を softprops/action-gh-release に変更する
  - @melpon
- [UPDATE] VP9, AV1 もサイマルキャストが利用可能となるよう入力チェックを変更する
  - @miosakuma
- [UPDATE] ヘルプテキストを修正する
  - @miosakuma
- [ADD] Ubuntu 22.04 x86_64 のビルドを追加
  - @melpon
- [ADD] `--sora-client-id` および `--sora-bundle-id` オプションを追加
  - @melpon
- [ADD] `--duration` および `--repeat-interval` オプションを追加
  - @melpon
- [ADD] `--max-retry` および `--retry-interval` オプションを追加
  - @melpon
- [ADD] 全てのインスタンスで duration が設定されていて、repeat-interval が設定されていない場合、全ての仮想クライアントが duration 時間経過することによって切断された時に zakuro を自動で終了する
  - @melpon
- [ADD] Lyra 向けオプションを追加
  - `--sora-audio-codec-type` オプションに `LYRA` を追加
  - `--sora-audio-codec-lyra-bitrate` オプションを追加
  - `--sora-audio-codec-lyra-usedtx` オプションを追加
  - `--sora-check-lyra-version` オプションを追加
  - @torikizi
- [FIX] 廃止になった `--sora-audio-opus-params-clock-rate` を削除する
  - @torikizi
- [FIX] "data-channels" の "interval" 項目を指定するとエラーになる問題を修正
  - @sile
- [FIX] クライアント名が消えてたのを修正
  - @melpon
- [FIX] パッケージを展開した際の名前が `zakuro` になってしまっていたのを `zakuro-{version}` にする
  - @melpon
  - @miosakuma
- [FIX] `--fake-audio-capture` が有効な場合に `--duration` が効かないのを修正
  - @melpon
- [FIX] `--output-file-connection-id` を指定してない場合、終了時に落ちてしまうのを修正
  - @melpon

## 2022.7.1 (2022-10-31)

- [FIX] CI でパッケージングしたアーカイブファイルが空になる問題を修正
  - @miosakuma

## 2022.7.0 (2022-10-06)

- [UPDATE] Sora C++ SDK を利用するように変更
  - @melpon
- [CHANGE] VCS を 100 から 1000 に増やす
  - @voluntas

## 2022.6.0 (2022-09-05)

- [UPDATE] asmjit と blend2d を最新版に上げる
  - @voluntas
- [UPDATE] `cmake` を `3.24.1` に上げる
  - @voluntas
- [UPDATE] yaml-cpp 0.7.0 に上げる
  - @voluntas
- [UPDATE] OpenH264 2.3.0 に上げる
  - @voluntas
- [UPDATE] WebRTC を m105.5195.0.0 に上げる
  - @melpon
- [UPDATE] Boost を 1.80.0 に上げる
  - @melpon

## 2022.5.0 (2022-07-06)

- [UPDATE] WebRTC を m103.5060.5.0 に上げる
  - @melpon @voluntas @torikizi

## 2022.3.0

- [CHANGE] `--hatch-rate` を `--vcs-hatch-rate` に変更
  - @melpon
- [ADD] `--instance-hatch-rate` を追加
  - @melpon
- [ADD] シグナリング URL への接続順序をランダム化して、それを無効化するオプション `--sora-disable-signaling-url-randomization` を追加
  - @melpon

## 2022.3.0

- [UPDATE] WebRTC を m102.5005.7.6 に上げる
  - @melpon @voluntas
- [UPDATE] asmjit と blend2d を最新版に上げる
  - @voluntas @melpon
- [UPDATE] `cmake` を `3.23.2` に上げる
  - @voluntas
- [UPDATE] OpenH264 2.2.0 に上げる
  - @voluntas
- [UPDATE] `CLI11` を `2.2.0` に上げる
  - @voluntas
- [UPDATE] Boost 1.79.0 に上げる
  - @voluntas
- [ADD] simulcast_rid を追加する
  - @melpon
- [ADD] spotlight_focus_rid を追加する
  - @melpon
- [ADD] spotlight_unfocus_rid を追加する
  - @melpon

## 2022.2.0

- [UPDATE] `cmake` を `3.22.3` に上げる
  - @voluntas
- [UPDATE] libwebrtc のバージョンを `m99.4844.1.0` に上げる
  - @voluntas
- [FIX] turn = false でも利用可能にする
  - @melpon

## 2022.1.0

- [ADD] `--client-cert` と `--client-key` でクライアント認証をできるようにする
  - @melpon
- [ADD] `--initial-mute-video` と `--initial-mute-audio` でミュート状態で接続できるようにする
  - @melpon
- [UPDATE] libwebrtc のバージョンを `m99.4844.0.0` に上げる
  - @melpon
- [UPDATE] Boost 1.78.0 に上げる
  - @melpon

## 2021.17.0

- [ADD] DataChannel メッセージングのランダムパケットの先頭部分に ConnecitonID を含める
  - `<<"ZAKURO", UnixTimeUs:64, LabelCounter:64, ConnectionID:26/binary, RandomBin/binary>>`
- [UPDATE] libwebrtc のバージョンを `m97.4692.0.0` に上げる
  - @voluntas

## 2021.16

- [CHANGE] DataChannel メッセージングのランダムパケットの先頭に ZAKURO という文字列をいれる
  - @melpon
- [UPDATE] `cmake` を `3.21.4` に上げる
  - @voluntas

## 2021.15

- [UPDATE] libwebrtc のバージョンを `m96.4664.1.1` に上げる
  - @tnoho @voluntas @melpon
- [CHANGE] `--use-dcsctp` フラグを削除（DcSCTP は常に有効になる）
  - @melpon

## 2021.14

- [UPDATE] libwebrtc のバージョンを `m96.4664.0.2` に上げる
  - @tnoho @voluntas

## 2021.13

- [CHANGE] data_channel_messaging を data_channles へ変更する
  - @torikizi
- [ADD] DataChannel メッセージングのデータに時刻とカウンターを追加
  - @melpon

## 2021.12

- [UPDATE] `CLI11` を `2.1.2` に上げる
  - @voluntas @melpon
- [UPDATE] libwebrtc のバージョンを `m94.4606.3.4` に上げる
  - @voluntas
- [FIX] Let's Encrypt な証明書の SSL 接続が失敗する問題を修正する
  - @melpon

## 2021.11

- [ADD] クラスター機能に対応
  - @melpon
- [UPDATE] シグナリングの mid に対応
  - @melpon
- [UPDATE] libwebrtc のバージョンを `m94.4606.3.3` に上げる
  - @voluntas
- [UPDATE] `cmake` を `3.21.3` に上げる
  - @voluntas
- [UPDATE] asmjit を `d0d14ac774977d0060a351f66e35cb57ba0bf59c` に上げる
  - @voluntas
- [UPDATE] blend2d を `3a0299c9126d19759a483ac3267a52b50ec77141` に上げる
  - @voluntas

## 2021.10

- [UPDATE] Boost 1.77.0 に上げる
  - @voluntas
- [UPDATE] libwebrtc のバージョンを `m93.4577.8.0` に上げる
  - @melpon
- [UPDATE] `cmake` を `3.21.2` に上げる
  - @voluntas
- [UPDATE] `CLI11` を `2.0.0` に上げる
  - @melpon
- [UPDATE] AES-GCM を候補に含める
  - @melpon
- [ADD] DataChannel メッセージングに対応
  - @melpon
- [ADD] DataChannel メッセージング向け負荷試験機能を追加
  - @melpon

## 2021.9

- [UPDATE] `cmake` を `3.20.5` に上げる
  - @voluntas
- [ADD] `--sora-audio-opus-params-clock-rate` オプションを追加
  - @melpon
- [FIX] fake 映像/音声が正常に動かなくなっていたのを修正
  - @melpon

## 2021.8

- [UPDATE] WebRTC のバージョンを M92 (4515@{#9}) に上げる
  - @melpon
- [ADD] dcsctp を利用するオプション `--use-dcsctp` を追加
  - @melpon

## 2021.7

- [CHANGE] Ubuntu 18.04 のビルドを削除
  - @melpon
- [ADD] YAML 設定で複数のインスタンスを立ち上げられる機能を実装
  - @melpon
- [ADD] パケロスなどの機能をオプションで指定可能にする
  - @melpon

## 2021.6

- [UPDATE] yaml の設定を引数で上書き可能にする
  - @melpon
- [FIX] 複数インスタンス時のキー入力がおかしくなるのを修正
  - @melpon

## 2021.5.2

- [FIX] セグフォやデッドロックを修正
  - @melpon

## 2021.5.1

- [FIX] DataChannel 周りの対応漏れを追加
  - @melpon

## 2021.5

- [UPDATE] WebRTC のバージョンを M91 (4472@{#9}) に上げる
  - @melpon
- [UPDATE] `cmake` を `3.20.4` に上げる
  - @voluntas
- [UPDATE] asmjit を `78de7d9c81a6ad1b0f732b52666960d9be1c6461` に上げる
  - @voluntas
- [FIX] DataChannel 周りの新しい仕様への追従
  - @melpon

## 2021.4

- [UPDATE] DataChannel の type: switch に対応
  - @melpon

## 2021.3

- [UPDATE] `cmake` を `3.20.2` に上げる
  - @voluntas
- [UPDATE] DataChannel シグナリングに対応
  - @melpon
- [UPDATE] WebSocket シグナリングの re-offer に対応
  - @melpon
- [FIX] サイマルキャストの active: false に対応
  - @melpon

## 2021.2

- [UPDATE] `cmake` を `3.20.1` に上げる
  - @voluntas
- [UPDATE] WebRTC のバージョンを M90 (4430@{#3}) に上げる
  - @melpon
- [UPDATE] Boost 1.76.0 に上げる
  - @voluntas
- [UPDATE] blend2d を `41e4f9f440ed2cef9f3d19699a14e9e7b11144e4` に上げる
  - @voluntas
- [UPDATE] asmjit を `0dd16b0a98ae1da48563c9cc62f757a9e6bbe9b6` に上げる
  - @voluntas

## 2021.1.2

- [UPDATE] 設定ファイルの内容を出力する際に引数をエスケープする
  - @melpon
- [FIX] 設定ファイルの `metadata` や `signaling-notify-metadata` などが引数に反映されていないのを修正
  - @melpon

## 2021.1.1

- [UPDATE] `cmake` を `3.20.0` に上げる
  - @voluntas
- [FIX] `--metadata` と `--signaling-notify-metadata` 引数に `--sora-` プリフィックスが付いてなかったのを修正
  - @melpon

## 2021.1

- [CHANGE] Sora 関連の引数には `--sora-` の prefix を追加する
  - @melpon
- [CHANGE] CentOS 8 を削除する
  - @voluntas
- [UPDATE] blend2d を `92ba4eaa2f22331bc9823ddb47f53dd8ce683c8b` に上げる
  - @voluntas
- [UPDATE] asmjit を `a4dd0b2d8b0fdbcda777e4d6dae0e76636080113` に上げる
  - @voluntas
- [UPDATE] `libwebrtc` を `M89.4389@{#7}` に上げる
  - @melpon
- [UPDATE] `cmake` を `3.19.4` に上げる
  - @voluntas
- [ADD] サイマルキャストの active と adaptivePtime に対応する
  - @melpon
- [UPDATE] nlohmann/json を Boost.JSON に変更
  - @melpon
- [ADD] 実験的機能として YAML ファイルへ対応する
  - @melpon
- [ADD] 実験的機能としてスポットライト利用時に音声ファイルを利用して好きな数字を発生できる機能を追加する
  - @melpon

## 2020.2

- [ADD] `--game=kuzushi` を実装
  - @melpon
- [UPDATE] `libwebrtc` を `M86.4240@{#10}` に上げる
  - @voluntas
- [UPDATE] `cmake` を `3.18.3` に上げる
  - @voluntas

## 2020.1

**祝リリース**
