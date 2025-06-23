# WebRTC Load Testing Tool Zakuro

[![libwebrtc](https://img.shields.io/badge/libwebrtc-m136.7103-blue.svg)](https://chromium.googlesource.com/external/webrtc/+/branch-heads/7103)
[![GitHub tag (latest SemVer)](https://img.shields.io/github/tag/shiguredo/zakuro.svg)](https://github.com/shiguredo/zakuro)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read <https://github.com/shiguredo/oss> before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に <https://github.com/shiguredo/oss> をお読みください。

## WebRTC Load Testing Tool Zakuro について

WebRTC Load Testing Tool Zakuro は [libwebrtc](https://webrtc.googlesource.com/src.git/) を利用した [WebRTC SFU Sora](https://sora.shiguredo.jp/) 向けの WebRTC 負荷試験ツールです。

## 特徴

- 最新の WebRTC SFU Sora に対応
- YAML によるシナリオファイルへ対応
- 動的インスタンス作成へ対応
- クラスター機能への対応
  - 複数シグナリング URL を指定できる
- フェイク音声/映像に対応
- リアルタイムメッセージング機能へ対応
- シグナリングのクライアント証明書 (mTLS) へ対応
- 最新の libwebrtc へ対応
- [OpenH264](https://www.openh264.org/) を利用した H.264 コーデックに対応
- [Sora C++ SDK](https://github.com/shiguredo/sora-cpp-sdk) ベース
- 期間繰り返し対応
  - 30 秒負荷かけて切断を繰り返すなど

## 使ってみる

Zakuro を使ってみたい人は [USE.md](doc/USE.md) をお読みください。

## ビルドする

Zakuro のビルドしたい人は [BUILD.md](doc/BUILD.md) をお読みください

## FAQ

[FAQ.md](doc/FAQ.md) をお読みください。

## ヘルプ

```console
$ ./zakuro --help
Zakuro - WebRTC Load Testing Tool
Usage: [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Show version information
  --show-video-codec-capability
                              Show available video codec capability
  --config TEXT:FILE          YAML config file path
  --log-level INT:value in {verbose->0,info->1,warning->2,error->3,none->4} OR {0,1,2,3,4}
                              Log severity level threshold
  --port INT:INT in [-1 - 65535]
                              Port number (default: -1)
  --output-file-connection-id TEXT
                              Output to specified file with connection IDs
  --instance-hatch-rate FLOAT:FLOAT in [0.1 - 100]
                              Spawned instance per seconds (default: 1.0)
  --name TEXT                 Client Name
  --vcs INT:INT in [1 - 1000] Virtual Clients (default: 1)
  --vcs-hatch-rate FLOAT:FLOAT in [0.1 - 100]
                              Spawned virtual clients per seconds (default: 1.0)
  --duration FLOAT            (Experimental) Duration of virtual client running in seconds (if not zero) (default: 0.0)
  --repeat-interval FLOAT     (Experimental) (If duration is set) Interval to reconnect after disconnection (default: 0.0)
  --max-retry INT             (Experimental) Max retries when a connection fails (default: 0)
  --retry-interval FLOAT      (Experimental) (If max-retry is set) Interval to reconnect after connection fails (default: 60)
  --no-video-device           Do not use video device (default: false)
  --no-audio-device           Do not use audio device (default: false)
  --fake-capture-device       Fake Capture Device (default: true)
  --fake-video-capture TEXT:FILE
                              Fake Video from File
  --fake-audio-capture TEXT:FILE
                              Fake Audio from File
  --sandstorm                 Fake Sandstorm Video (default: false)
  --video-device TEXT         Use the video device specified by an index or a name (use the first one if not specified)
  --resolution TEXT           Video resolution (one of QVGA, VGA, HD, FHD, 4K, or [WIDTH]x[HEIGHT]) (default: VGA)
  --framerate INT:INT in [1 - 60]
                              Video framerate (default: 30)
  --fixed-resolution          Maintain video resolution in degradation (default: false)
  --priority TEXT:{BALANCE,FRAMERATE,RESOLUTION}
                              (Experimental) Preference in video degradation (default: BALANCE)
  --insecure                  Allow insecure server connections when using SSL (default: false)
  --openh264 TEXT:FILE        OpenH264 dynamic library path. "OpenH264 Video Codec provided by Cisco Systems, Inc."
  --game TEXT:{kuzushi}       Play game
  --scenario TEXT:{reconnect} Scenario type
  --client-cert TEXT:FILE     Cert file path for client certification (PEM format)
  --client-key TEXT:FILE      Private key file path for client certification (PEM format)
  --initial-mute-video BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Mute video initialy (default: false)
  --initial-mute-audio BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Mute audio initialy (default: false)
  --degradation-preference ENUM:value in {disabled->0,maintain_framerate->1,maintain_resolution->2,balanced->3} OR {0,1,2,3}
                              Degradation preference
  --sora-signaling-url TEXT ...
                              Signaling URLs
  --sora-disable-signaling-url-randomization
                              Disable random connections to signaling URLs (default: false)
  --sora-channel-id TEXT      Channel ID
  --sora-client-id TEXT       Client ID
  --sora-bundle-id TEXT       Bundle ID
  --sora-role TEXT:{sendonly,recvonly,sendrecv}
                              Role
  --sora-video BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Send video to sora (default: true)
  --sora-audio BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Send audio to sora (default: true)
  --sora-video-codec-type TEXT:{VP8,VP9,AV1,H264,H265}
                              Video codec for send (default: none)
  --sora-audio-codec-type TEXT:{OPUS}
                              Audio codec for send (default: none)
  --sora-video-bit-rate INT:INT in [0 - 30000]
                              Video bit rate (default: none)
  --sora-audio-bit-rate INT:INT in [0 - 510]
                              Audio bit rate (default: none)
  --sora-simulcast BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use simulcast (default: false)
  --sora-simulcast-rid TEXT   Simulcast rid (default: none)
  --sora-spotlight BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use spotlight (default: none)
  --sora-spotlight-number INT:INT in [0 - 8]
                              Number of spotlight (default: none)
  --sora-spotlight-focus-rid TEXT
                              Spotlight focus rid (default: none)
  --sora-spotlight-unfocus-rid TEXT
                              Spotlight unfocus rid (default: none)
  --sora-data-channel-signaling TEXT:value in {false->,true->,none->} OR {}
                              Use DataChannel for Sora signaling (default: none)
  --sora-data-channel-signaling-timeout INT:POSITIVE
                              Timeout for Data Channel in seconds (default: 180)
  --sora-ignore-disconnect-websocket TEXT:value in {false->,true->,none->} OR {}
                              Ignore WebSocket disconnection if using Data Channel (default: none)
  --sora-disconnect-wait-timeout INT:POSITIVE
                              Disconnecting timeout for Data Channel in seconds (default: 5)
  --sora-metadata TEXT:JSON Value
                              Signaling metadata used in connect message (default: none)
  --sora-signaling-notify-metadata TEXT:JSON Value
                              Signaling metadata (default: none)
  --sora-data-channels TEXT:JSON Value
                              DataChannels (default: none)
  --sora-video-vp9-params TEXT:JSON Value
                              Parameters for VP9 video codec (default: none)
  --sora-video-av1-params TEXT:JSON Value
                              Parameters for AV1 video codec (default: none)
  --sora-video-h264-params TEXT:JSON Value
                              Parameters for H.264 video codec (default: none)
  --sora-video-h265-params TEXT:JSON Value
                              Parameters for H.265 video codec (default: none)
  --vp8-encoder ENUM:(internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf)
                              VP8 encoder implementation
  --vp9-encoder ENUM:(internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf)
                              VP9 encoder implementation
  --av1-encoder ENUM:(internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf)
                              AV1 encoder implementation
  --h264-encoder ENUM:(internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf)
                              H.264 encoder implementation
  --h265-encoder ENUM:(internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf)
                              H.265 encoder implementation
```

## ライセンス

Apache License 2.0

```
Copyright 2020-2025, Wandbox LLC (Original Author)
Copyright 2020-2025, Shiguredo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## OpenH264

<https://www.openh264.org/BINARY_LICENSE.txt>

```text
"OpenH264 Video Codec provided by Cisco Systems, Inc."
```

### 音声ファイルのライセンス

スポットライト機能検証時に利用する音声ファイルには [あみたろの声素材工房](https://amitaro.net/) 様の声素材を使用しています。

## 優先実装

優先実装とは Sora のライセンスを契約頂いているお客様限定で Zakuro の実装予定機能を有償にて前倒しで実装することです。

### 優先実装が可能な機能一覧

**詳細は Discord やメールなどでお気軽にお問い合わせください**

- Content Hint への対応
- --fake-video-capture で mjpeg も指定可能にする
- --audio-device 追加
