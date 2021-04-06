# Zakuro を使ってみる

まずは [FAQ.md](FAQ.md) や 制限をよくお読みください。

## バイナリを取得する

以下からダウンロードしてください。

https://github.com/shiguredo/zakuro/releases

## セットアップ

### Ubuntu

```shell
$ sudo apt update
$ sudo apt install libnspr4 libnss3
```

## 負荷をかけてみる

シグナリング URL は wss:// から始めてください。

負荷をかける先は必ず自社が運用している Sora にしてください。

まず 5 人の会議を行う簡単な負荷をかけてみましょう


```
$ ./zakuro \

    --sora-signaling-url wss://example.com/signaling \
    --sora-role sendrecv \
    --sora-channel-id zakuro-test \
    --sora-video-codec-type VP8 \
    --sora-video-bit-rate 1000 \
    --sora-multistream true \
    --resolution 640x480 \
    --fake-capture-device \
    --vcs 5
```


上記コマンドを実行することで負荷が走ります。それぞれの項目については -h を見てください。
おそらく Sora を理解していればわからないことは特に無いと思います。

## Zakuro 特有の機能

### 仮想クライアント

`--vcs 5`

Zakuro では Virtual Clients (vcs) で仮想クライアント数を指定できます。

指定可能な値は 100 ですが、100 は想像以上に CPU が必要になるので注意してください。

### フェイクキャプチャデバイス

`--fake-capture-device`

Zakuro ではサーバで起動することを想定し、音声と映像のフェイクデバイスを用意しています。

このフェイクデバイスは WebKit の開発メニューから利用できるモックキャプチャデバイスを [Blend2D](https://blend2d.com/) にて移植したものです。

### 砂嵐

`--sandstorm`

Zakuro ではエンコーダとデコーダに負荷をかけるために砂嵐を生成する仕組みが入っています。

砂嵐は VGA でも 30fps 利用する場合相当なビットレートと CPU が必要になるので注意してください。

### ハッチレート

`--hatch-rate 1`

Zakuro では 1 秒間に起動する仮想クライアント数を指定できます。デフォルトは 1 秒 1 仮想クライアントです。
基本的にはデフォルトで問題ありません。

同時に 2 台接続しに来たときの負荷を実現したいときなどに利用してみてください。

### OpenH264

`--openh264 /path/to/libopenh264-2.1.1-linux64.6.so`

Zakuro ではソフトウェアエンコーダを OpenH264 のライブラリを Dynamic Link することで利用可能です。

OpenH264 のバイナリの最新版は以下からダウンロード可能です。

https://github.com/cisco/openh264/releases/tag/v2.1.1


### 音声ファイル指定

`--fake-audio-capture /path/to/sample.wav`

Zakuro ではマイクからの音声入力の代わりに wav ファイルを指定することが可能です。

### 映像ファイル指定

`--fake-video-capture /path/to/sample.y4m`

Zakuro ではカメラからの映像入力の代わりに y4m ファイルを指定することが可能です。

### YAML 設定

**この機能はまだ実験的な機能です**

```yaml
zakuro:
  log-level: none
  port: -1
  instances:
    - name: zakuro1
      vcs: 2
      sora:
        signaling-url: "wss://sora.example.com/signaling"
        channel-id: "sora"
        role: "sendrecv"
        video-codec-type: VP8
        multistream: true
        spotlight: true
        simulcast: true
    - name: zakuro2
      vcs: 2
      sora:
        signaling-url: "wss://sora.example.com/signaling"
        channel-id: "sora"
        role: "sendrecv"
        video-codec-type: VP8
        multistream: true
        spotlight: true
        simulcast: true
```
