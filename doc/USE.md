# 利用方法

まずは制限をよくお読みください。

## 制限

**現時点ではかなり制限が多いです**

現時点で 1 つの Zakuro で 1 つのチャネルでにしか負荷をかけられません。
そのため複数のチャネルに負荷をかける場合は Zakuro を複数起動してください。

チャネル以外の項目も全て 1 つに固定されます。

## バイナリを取得する

以下からダウンロードしてください。

https://github.com/shiguredo/zakuro/releases

## セットアップ

### Ubuntu

```
apt install libnspr4 libnss3
```

### CentOS

TBD


## コマンドライン

- シグナリング URL
    - wss://example.com/signaling
- ロール
    - 送受信
- チャネル ID
    - zakuro-test
- 音声コーデック
    - OPUS
- 映像コーデック
    - VP8
- 映像ビットレート
    - 800kbps
- マルチストリーム
    - 有効
- フェイクキャプチャデバイス

```
$ ./zakuro wss://example.com/signaling \
    --auto \
    --fake-capture-device \
    --role sendrecv \
    --channel-id zakuro-test \
    --audio true \
    --audio-codec-type OPUS \
    --resolution 640x480 \
    --video true \
    --video-codec-type VP8 \
    --video-bit-rate 800 \
    --multistream true \
    --vcs 10
```

### ヘルプ


```
$ ./zakuro --help
Zakuro - WebRTC Load Testing Tool
Usage: ./zakuro [OPTIONS] SIGNALING-URL

Positionals:
  SIGNALING-URL TEXT REQUIRED Signaling URL

Options:
  -h,--help                   Print this help message and exit
  --help-all                  Print help message for all modes and exit
  --vcs INT:INT in [1 - 100]  Virtual Clients
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
  --version                   Show version information
  --insecure                  Allow insecure server connections when using SSL
  --log-level INT:value in {verbose->0,info->1,warning->2,error->3,none->4} OR {0,1,2,3,4}
                              Log severity level threshold
  --openh264 TEXT:FILE        OpenH264 dynamic library path
  --channel-id TEXT REQUIRED  Channel ID
  --auto                      Connect to Sora automatically
  --video BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Send video to sora (default: true)
  --audio BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Send audio to sora (default: true)
  --video-codec-type TEXT:{,AV1,H264,VP8,VP9}
                              Video codec for send
  --audio-codec-type TEXT:{,OPUS}
                              Audio codec for send
  --video-bit-rate INT:INT in [0 - 30000]
                              Video bit rate
  --audio-bit-rate INT:INT in [0 - 510]
                              Audio bit rate
  --role TEXT:{recvonly,sendonly,sendrecv} REQUIRED
                              Role
  --multistream BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use multistream (default: false)
  --simulcast BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use simulcast (default: false)
  --spotlight BOOLEAN:value in {false->0,true->1} OR {0,1}
                              Use spotlight (default: false)
  --spotlight-number INT:INT in [0 - 8]
                              Number of spotlight
  --port INT:INT in [-1 - 65535]
                              Port number (default: -1)
  --metadata TEXT:JSON Value  Signaling metadata used in connect message
  --signaling-notify-metadata TEXT:JSON Value
                              Signaling metadata
```
