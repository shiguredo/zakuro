# FAQ

## WebRTC Load Testing Tool Zakuro とはなんですか？

[株式会社時雨堂](https://shiguredo.jp) が開発/販売している [WebRTC SFU Sora](https://sora.shiguredo.jp) 専用の負荷試験ツールです。

## Zakuro は何に利用するのですか？

Sora を運用するサーバーのサイジングの見積もりが目的です。
WebRTC は要求するビットレートや利用するネットワーク、送信者、受信者の数によって負荷が変わります。

そのため今まではサイジングがとても難しく、ある程度余裕のあるスペックを用意するしかありませんでした。

Zakuro を提供することで、 Sora を運用するサーバーのサイジングを行えるようにするのが目的です。

## Zakuro は無料で利用できますか？

[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) としてソースコードを公開しています。

## Zakuro のライセンスを教えてください

[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) です。

## Zakuro に含まれている音声ファイルについて教えてください

Zakuro のスポットライト機能検証時に利用する音声ファイルには [あみたろの声素材工房](https://amitaro.net/) 様の声素材を使用しています。

## Zakuro の動作環境を教えてください

サーバーでの用途を前提としているため Linux での動作を想定しています。ただし簡易的な検証をできるように macOS arm64 でも利用できます。

- Ubuntu 24.04 x86_64
- Ubuntu 22.04 x86_64
- macOS arm64

## macOS 版のバイナリは公開されますか？

macOS arm64 版のバイナリは公開しておりません。自前ビルドをお願いします。

## Zakuro は破壊的変更を行いますか？

積極的な破壊的変更を行います。

## Zakuro は IPv6 に対応していますか？

対応しています。

Ubuntu サーバーを利用している場合、
MACアドレスベースの EUI-64 フォーマットの IPv6 になっている場合があります。
これは非推奨となっているため、 sysctl で `add_gen_mode` を確認し変更してください。

## Zakuro を利用する際の注意点はありますか？

### シグナリングで利用する WebSocket は wss のみ対応

Zakuro は `wss://` のみ対応しています。 `ws://` には対応していません。

### ファイルディスクリプタ数

かなりの数を消費しますので、 Zakuro と Sora 両方のサーバーで多めに設定することを推奨します。

### 負荷をかける対象

Zakuro は Sora に膨大な負荷をかけますので、利用する場合はとても注意してください。

ツール利用者が運用していない環境への利用は絶対に行わないようにしてください。

### サーバースペック

これは効率の良いシミュレータではなく力技で負荷を実現するツールです。

そのため、大量の CPU リソースを消費します。利用する場合は多くのコアを積んだサーバーを用意してください。

## 実際のブラウザなどと異なりますか？

異なります。

ただし libwebrtc をベースにしているため、 WebRTC の挙動自体はブラウザとほぼ同じです。
さらに 1 接続 1 エンコーダを採用しているため、 1 接続が独立しています。

そのため再送や全画面要求に対してもそれぞれの接続ごとに処理を行います。

## 受信した音声や映像をデコードしていますか？

していません。負荷を削減するためデコードする前に破棄しています。

## エンコーダやデコーダに負荷をかけるにはどうすればいいですか？

砂嵐を生成する `--sandstorm` を利用してみてください。ただしこれは凄まじく CPU とネットワークを消費します。

## mp4 から y4m はどうやって作成すればいいですか？

FFmpeg を利用すると簡単に作成できます。

```bash
ffmpeg -i in.mp4 -f yuv4mpegpipe out.y4m
```

## mp4 ファイルから wav ファイルはどうやって作成すればいいですか？

```bash
ffmpeg -i in.mp4 -f wav -vn out.wav
```

## Ubuntu で zakuro から送信した H.264 の映像が受信できません

まずは `--show-video-codec-capability` を実行して H.264 のエンコーダーが利用可能かを確認してください。
その後、利用可能だったハードウェアエンコーダーを指定してください。

- Intel Video Processing Library (VPL) を利用する場合
  - `--h264-encoder intel_vpl` を指定してください
- NVIDIA Video Codec SDK を利用する場合
  - `--h264-encoder nvidia_video_codec_sdk` を指定してください
- AMD Advanced Media Framework (AMF) を利用する場合
  - `--h264-encoder amd_amf` を指定してください
- 上記エンコーダーを利用しない場合は OpenH264 をダウンロードして利用してください
  - [--openH264](https://github.com/shiguredo/zakuro/blob/master/doc/USE.md#openh264) を指定してください

## Ubuntu で zakuro から送信した H.265 の映像が受信できません

適切な H.265 エンコーダーが指定されているかを確認してください。

H.265 の送信にはハードウェアエンコーダーが必須です。

まずは `--show-video-codec-capability` を実行して H.265 のエンコーダーが利用可能かを確認してください。
その後、利用可能だったハードウェアエンコーダーを指定してください。

- Intel Video Processing Library (VPL) を利用する場合
  - `--h265-encoder intel_vpl` を指定してください
- NVIDIA Video Codec SDK を利用する場合
  - `--h265-encoder nvidia_video_codec_sdk` を指定してください
- AMD Advanced Media Framework (AMF) を利用する場合
  - `--h265-encoder amd_amf` を指定してください
