# Zakuro をビルドする

まずは Zakuro のリポジトリをダウンロードします。

```shell
$ git clone git@github.com:shiguredo/zakuro.git
```

## Ubuntu 20.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py ubuntu-20.04_x86_64` と打つことで Zakuro の Ubuntu 20.04 x86_64 向けバイナリが生成されます。

```shell
$ python3 run.py ubuntu-20.04_x86_64
```

うまくいかない場合は `rm -rf _source _build _install && python3 run.py ubuntu-20.04_x86_64` を試してみてください。