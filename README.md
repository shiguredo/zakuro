# WebRTC Load Testing Tool Zakuro

[![libwebrtc](https://img.shields.io/badge/libwebrtc-m96.4664-blue.svg)](https://chromium.googlesource.com/external/webrtc/+/branch-heads/4664)
[![GitHub tag (latest SemVer)](https://img.shields.io/github/tag/shiguredo/zakuro.svg)](https://github.com/shiguredo/zakuro)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read https://github.com/shiguredo/oss before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に https://github.com/shiguredo/oss をお読みください。

## WebRTC Load Testing Tool Zakuro について

WebRTC Load Testing Tool Zakuro は libwebrtc を利用した WebRTC SFU Sora 向けの WebRTC 負荷試験ツールです。

## 特徴

- WebRTC SFU Sora 開発版に対応
- YAML による設定ファイルへ対応
- 動的インスタンス作成へ対応
- クラスター機能への対応
- データチャネルメッセージング機能へ対応
- フェイクネットワークへ対応
- 最新の libwebrtc へ対応

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
  --config TEXT:FILE          YAML config file path
  --log-level INT:value in {verbose->0,info->1,warning->2,error->3,none->4} OR {0,1,2,3,4}
                              Log severity level threshold
  --port INT:INT in [-1 - 65535]
                              Port number (default: -1)
  --name TEXT                 Client Name
  --vcs INT:INT in [1 - 100]  Virtual Clients
  --hatch-rate FLOAT:FLOAT in [0.1 - 100]
                              Spawned virtual clients per seconds
  --no-video-device           Do not use video device
  --no-audio-device           Do not use audio device
  --fake-capture-device       Fake Capture Device
  --fake-video-capture TEXT:FILE
                              Fake Video from File
  --fake-audio-capture TEXT:FILE
                              Fake Audio from File
  --sandstorm                 Fake Sandstorm Video
  --video-device TEXT         Use the video device specified by an index or a name (use the first one if not specified)
  --resolution TEXT           Video resolution (one of QVGA, VGA, HD, FHD, 4K, or [WIDTH]x[HEIGHT])
  --framerate INT:INT in [1 - 60]
                              Video framerate
  --fixed-resolution          Maintain video resolution in degradation
  --priority TEXT:{BALANCE,FRAMERATE,RESOLUTION}
                              Preference in video degradation (experimental)
  --insecure                  Allow insecure server connections when using SSL
  --openh264 TEXT:FILE        OpenH264 dynamic library path. "OpenH264 Video Codec provided by Cisco Systems, Inc."
  --game TEXT:{kuzushi}       Play game
  --scenario TEXT:{reconnect} Scenario type
  --sora-signaling-url TEXT ...
                              Signaling URLs
  --sora-channel-id TEXT      Channel ID
  --sora-role TEXT:{sendonly,recvonly,sendrecv}
                              Role
  --sora-video BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Send video to sora (default: true)
  --sora-audio BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Send audio to sora (default: true)
  --sora-video-codec-type TEXT:{VP8,VP9,AV1,H264}
                              Video codec for send
  --sora-audio-codec-type TEXT:{OPUS}
                              Audio codec for send
  --sora-video-bit-rate INT:INT in [0 - 30000]
                              Video bit rate
  --sora-audio-bit-rate INT:INT in [0 - 510]
                              Audio bit rate
  --sora-audio-opus-params-clock-rate INT:INT in [1600 - 48000]
                              OPUS clock rate
  --sora-multistream BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use multistream (default: false)
  --sora-simulcast BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use simulcast (default: false)
  --sora-spotlight BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use spotlight (default: false)
  --sora-spotlight-number INT:INT in [0 - 8]
                              Number of spotlight
  --sora-data-channel-signaling TEXT:value in {false-> 0,true-> 1,none->--} OR { 0, 1,--}
                              Use DataChannel for Sora signaling (default: none)
  --sora-data-channel-signaling-timeout INT:POSITIVE
                              Timeout for Data Channel in seconds (default: 180)
  --sora-ignore-disconnect-websocket TEXT:value in {false-> 0,true-> 1,none->--} OR { 0, 1,--}
                              Ignore WebSocket disconnection if using Data Channel (default: none)
  --sora-disconnect-wait-timeout INT:POSITIVE
                              Disconnecting timeout for Data Channel in seconds (default: 5)
  --sora-metadata TEXT:JSON Value
                              Signaling metadata used in connect message
  --sora-signaling-notify-metadata TEXT:JSON Value
                              Signaling metadata
  --sora-data-channels TEXT:JSON Value
                              DataChannels
  --fake-network-send-queue-length-packets UINT
                              Queue length in number of packets for sending
  --fake-network-send-queue-delay-ms INT
                              Delay in addition to capacity induced delay for sending
  --fake-network-send-delay-standard-deviation-ms INT
                              Standard deviation of the extra delay for sending
  --fake-network-send-link-capacity-kbps INT
                              Link capacity in kbps for sending
  --fake-network-send-loss-percent INT
                              Random packet loss for sending
  --fake-network-send-allow-reordering BOOLEAN:value in {false->0,true->1} OR {0,1}
                              If packets are allowed to be reordered for sending
  --fake-network-send-avg-burst-loss-length INT
                              The average length of a burst of lost packets for sending
  --fake-network-send-packet-overhead INT
                              Additional bytes to add to packet size for sending
  --fake-network-send-codel-active-queue-management BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Enable CoDel active queue management for sending
  --fake-network-receive-queue-length-packets UINT
                              Queue length in number of packets for receiving
  --fake-network-receive-queue-delay-ms INT
                              Delay in addition to capacity induced delay for receiving
  --fake-network-receive-delay-standard-deviation-ms INT
                              Standard deviation of the extra delay for receiving
  --fake-network-receive-link-capacity-kbps INT
                              Link capacity in kbps for receiving
  --fake-network-receive-loss-percent INT
                              Random packet loss for receiving
  --fake-network-receive-allow-reordering BOOLEAN:value in {false->0,true->1} OR {0,1}
                              If packets are allowed to be reordered for receiving
  --fake-network-receive-avg-burst-loss-length INT
                              The average length of a burst of lost packets for receiving
  --fake-network-receive-packet-overhead INT
                              Additional bytes to add to packet size for receiving
  --fake-network-receive-codel-active-queue-management BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Enable CoDel active queue management for receiving
```


## ライセンス

Apache License 2.0

```
Copyright 2020-2021, Wandbox LLC (Original Author)
Copyright 2020-2021, Shiguredo Inc.

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

### 音声ファイルのライセンス

スポットライト機能検証時に利用する音声ファイルには [あみたろの声素材工房](http://www14.big.or.jp/~amiami/happy/) 様の声素材を使用しています。

[ゲーム等に使えるフリー声素材配布ページ \- あみたろの声素材工房](https://www14.big.or.jp/~amiami/happy/voice.html)

## 優先実装

優先実装とは Sora のライセンスを契約頂いているお客様限定で Zakuro の実装予定機能を有償にて前倒しで実装することです。

### 優先実装が可能な機能一覧

**詳細は Discord やメールなどでお気軽にお問い合わせください**

- Content Hint への対応
- --fake-video-capture で mjpeg も指定可能にする
- --audio-device 追加

