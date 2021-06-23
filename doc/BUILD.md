# Zakuro をビルドする

まずは Zakuro のリポジトリをダウンロードします。

```shell
$ git clone git@github.com:shiguredo/zakuro.git
```

## Docker の利用について

Zakuro をビルドする際には Docker 19.03 以降が必要になりますので、事前にインストールしておいてください。

Docker for Windows では未検証です。Linux 版、または Docker for Mac をご利用ください。

また、./build.sh 実行時に --no-mount オプションを指定することで、
マウントを利用しないモードで docker container を動作させることができます。何らかの理由でマウントがうまく動作しない場合に使って下さい。

## Ubuntu 20.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `./build.sh ubuntu-20.04_x86_64` と打つことで Zakuro の Ubuntu 20.04 x86_64 向けバイナリが生成されます。

```shell
$ ./build.sh ubuntu-20.04_x86_64
```

うまくいかない場合は `./build.sh --clean ubuntu-20.04_x86_64 && ./build.sh ubuntu-20.04_x86_64` を試してみてください。