# Zakuro をビルドする

まずは Zakuro のリポジトリをダウンロードします。

```shell
$ git clone git@github.com:shiguredo/zakuro.git
```

## 必要なライブラリのインストール

```console
$ sudo apt install libxext-dev libx11-dev libdrm-dev libva-dev pkg-config python3
```

## Ubuntu 22.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py ubuntu-22.04_x86_64` と打つことで Zakuro の Ubuntu 22.04 x86_64 向けバイナリが生成されます。

```shell
$ python3 run.py ubuntu-22.04_x86_64
```

うまくいかない場合は `rm -rf _source _build _install && python3 run.py ubuntu-22.04_x86_64` を試してみてください。

## Ubuntu 24.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py ubuntu-24.04_x86_64` と打つことで Zakuro の Ubuntu 24.04 x86_64 向けバイナリが生成されます。

```shell
$ python3 run.py ubuntu-24.04_x86_64
```

うまくいかない場合は `rm -rf _source _build _install && python3 run.py ubuntu-24.04_x86_64` を試してみてください。

