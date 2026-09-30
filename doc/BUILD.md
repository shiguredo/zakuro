# Zakuro をビルドする

まずは Zakuro のリポジトリをダウンロードします。

```shell
git clone git@github.com:shiguredo/zakuro.git
```

## macOS (arm64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py build macos_arm64` と打つことで Zakuro の macOS 15 以降の arm64 向けバイナリが生成されます。

```shell
python3 run.py build macos_arm64
```

うまくいかない場合は `git clean -ffdx && python3 run.py build macos_arm64` を試してみてください。

## Ubuntu 向けバイナリを作成する

### 必要なライブラリのインストール

```console
sudo apt install libxext-dev libx11-dev python3
```

Ubuntu 26.04 (arm64) 向けバイナリをクロスコンパイルする場合は aarch64 のリンカもインストールします。

```console
sudo apt install binutils-aarch64-linux-gnu
```

### Ubuntu 26.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py build ubuntu-26.04_x86_64` と打つことで Zakuro の Ubuntu 26.04 x86_64 向けバイナリが生成されます。

```shell
python3 run.py build ubuntu-26.04_x86_64
```

うまくいかない場合は `git clean -ffdx && python3 run.py build ubuntu-26.04_x86_64` を試してみてください。

### Ubuntu 26.04 (arm64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py build ubuntu-26.04_armv8` と打つことで Zakuro の Ubuntu 26.04 arm64 向けバイナリが生成されます。

x86_64 のホストからクロスコンパイルします。ビルド時に sysroot を `_install/ubuntu-26.04_armv8/release/rootfs` に生成します。libwebrtc が提供する clang は x86_64 向けのため、x86_64 のホストで実行してください。

```shell
python3 run.py build ubuntu-26.04_armv8
```

うまくいかない場合は `git clean -ffdx && python3 run.py build ubuntu-26.04_armv8` を試してみてください。

### Ubuntu 24.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py build ubuntu-24.04_x86_64` と打つことで Zakuro の Ubuntu 24.04 x86_64 向けバイナリが生成されます。

```shell
python3 run.py build ubuntu-24.04_x86_64
```

うまくいかない場合は `git clean -ffdx && python3 run.py build ubuntu-24.04_x86_64` を試してみてください。

### Ubuntu 22.04 (x86_64) 向けバイナリを作成する

build ディレクトリ以下で `python3 run.py build ubuntu-22.04_x86_64` と打つことで Zakuro の Ubuntu 22.04 x86_64 向けバイナリが生成されます。

```shell
python3 run.py build ubuntu-22.04_x86_64
```

うまくいかない場合は `git clean -ffdx && python3 run.py build ubuntu-22.04_x86_64` を試してみてください。
