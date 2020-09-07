#!/bin/bash

set -e

# ヘルプ表示
function show_help() {
  echo ""
  echo "$0 <作業ディレクトリ> <Zakuroリポジトリのルートディレクトリ> <マウントタイプ [mount | nomount]> <パッケージ名> <Dockerイメージ名> <ZAKURO_COMMIT>"
  echo ""
}

# 引数のチェック
if [ $# -ne 6 ]; then
  show_help
  exit 1
fi

WORK_DIR="$1"
ZAKURO_DIR="$2"
MOUNT_TYPE="$3"
PACKAGE_NAME="$4"
DOCKER_IMAGE="$5"
ZAKURO_COMMIT="$6"

if [ -z "$WORK_DIR" ]; then
  echo "エラー: <作業ディレクトリ> が空です"
  show_help
  exit 1
fi

if [ -z "$ZAKURO_DIR" ]; then
  echo "エラー: <Zakuroリポジトリのルートディレクトリ> が空です"
  show_help
  exit 1
fi

if [ ! -e "$ZAKURO_DIR/.git" ]; then
  echo "エラー: $ZAKURO_DIR は Git リポジトリのルートディレクトリではありません"
  show_help
  exit 1
fi

if [ "$MOUNT_TYPE" != "mount" -a "$MOUNT_TYPE" != "nomount"  ]; then
  echo "エラー: <マウントタイプ> は mount または nomount である必要があります"
  show_help
  exit 1
fi

if [ -z "$PACKAGE_NAME" ]; then
  echo "エラー: <パッケージ名> が空です"
  show_help
  exit 1
fi

if [ -z "$(docker images -q $DOCKER_IMAGE)" ]; then
  echo "エラー: <Dockerイメージ名> $DOCKER_IMAGE が存在しません"
  show_help
  exit 1
fi

if [ -z "$ZAKURO_COMMIT" ]; then
  echo "エラー: <ZAKURO_COMMIT> が空です"
  show_help
  exit 1
fi

if [ ! -e "$ZAKURO_DIR/VERSION" ]; then
  echo "エラー: $ZAKURO_DIR/VERSION が存在しません"
  exit 1
fi

source $ZAKURO_DIR/VERSION

# マウントするかどうかで大きく分岐する
if [ "$MOUNT_TYPE" = "mount" ]; then
  # マウントする場合は、単純にマウントしてビルドするだけ
  docker run \
    -it \
    --rm \
    -v "$WORK_DIR/..:/root/zakuro" \
    "$DOCKER_IMAGE" \
    /bin/bash -c "
      set -ex
      source /root/webrtc/VERSIONS
      mkdir -p /root/zakuro/_build/$PACKAGE_NAME
      pushd /root/zakuro/_build/$PACKAGE_NAME
        cmake \
          -DCMAKE_BUILD_TYPE=Release \
          -DZAKURO_PACKAGE_NAME=$PACKAGE_NAME \
          -DZAKURO_VERSION=$ZAKURO_VERSION \
          -DZAKURO_COMMIT=$ZAKURO_COMMIT \
          -DWEBRTC_BUILD_VERSION=$WEBRTC_BUILD_VERSION \
          -DWEBRTC_READABLE_VERSION=\$WEBRTC_READABLE_VERSION \
          -DWEBRTC_COMMIT=\$WEBRTC_COMMIT \
          ../..
        if [ -n \"$VERBOSE\" ]; then
          export VERBOSE=$VERBOSE
        fi
        cmake --build . -j\$(nproc)
        cp /root/webrtc/NOTICE /root/zakuro/_build/$PACKAGE_NAME/NOTICE
      popd
    "
else
  # マウントしない場合は、コンテナを起動して、コンテナに必要なファイルを転送して、コンテナ上でビルドして、生成されたファイルをコンテナから戻して、コンテナを終了する

  pushd $ZAKURO_DIR
    if git diff-index --quiet HEAD --; then
      :
    else
      # ローカルの変更があるので確認する
      git status
      read -p "ローカルの変更があります。これらの変更はビルドに反映されません。続行しますか？ (y/N): " yn
      case "$yn" in
        [yY]*)
          ;;
        *)
          exit 1
          ;;
      esac
    fi
  popd

  pushd $WORK_DIR
    # 途中でエラーが起きても確実にコンテナを後片付けする
    trap "set +e; docker container stop zakuro-$PACKAGE_NAME; docker container rm zakuro-$PACKAGE_NAME" 0

    # ベースイメージから構築したコンテナに転送してビルドし、
    # ビルドが完了したら成果物や中間ファイルを取り出す
    docker container create -it --name zakuro-$PACKAGE_NAME "$DOCKER_IMAGE"
    docker container start zakuro-$PACKAGE_NAME

    # 転送用の zakuro のソースを生成（中間ファイルも含める）
    rm -rf zakuro
    git clone $ZAKURO_DIR zakuro

    # 中間ファイルのコピー
    mkdir -p $ZAKURO_DIR/_build
    if [ -e $ZAKURO_DIR/_build/$PACKAGE_NAME ]; then
      mkdir -p zakuro/_build
      cp -r $ZAKURO_DIR/_build/$PACKAGE_NAME zakuro/_build/$PACKAGE_NAME
    fi

    # 更新日時を元ファイルに合わせる
    pushd zakuro
      find . -type f | while read file; do
        if [ -e "$ZAKURO_DIR/$file" ]; then
          # -c: ファイルを生成しない
          # -m: 更新日時を更新
          # -r <file>: このファイルの日時に合わせる
          touch -c -m -r "$ZAKURO_DIR/$file" "$file"
        fi
      done
    popd

    tar czf zakuro.tar.gz zakuro
    rm -rf zakuro

    # ソースを転送して Docker の中でビルドする
    docker container cp zakuro.tar.gz zakuro-$PACKAGE_NAME:/root/
    rm zakuro.tar.gz

    docker container exec zakuro-$PACKAGE_NAME /bin/bash -c 'cd /root && tar xf zakuro.tar.gz && rm zakuro.tar.gz'
    docker container exec zakuro-$PACKAGE_NAME \
      /bin/bash -c "
        set -ex
        source /root/webrtc/VERSIONS
        mkdir -p /root/zakuro/_build/$PACKAGE_NAME
        pushd /root/zakuro/_build/$PACKAGE_NAME
          cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DZAKURO_PACKAGE_NAME=$PACKAGE_NAME \
            -DZAKURO_VERSION=$ZAKURO_VERSION \
            -DZAKURO_COMMIT=$ZAKURO_COMMIT \
            -DWEBRTC_BUILD_VERSION=$WEBRTC_BUILD_VERSION \
            -DWEBRTC_READABLE_VERSION=\$WEBRTC_READABLE_VERSION \
            -DWEBRTC_COMMIT=\$WEBRTC_COMMIT \
            ../..
          if [ -n \"$VERBOSE\" ]; then
            export VERBOSE=$VERBOSE
          fi
          cmake --build . -j\$(nproc)
          cp /root/webrtc/NOTICE /root/zakuro/_build/$PACKAGE_NAME/NOTICE
        popd
      "

    # 中間ファイル類を取り出す
    rm -rf $ZAKURO_DIR/_build/$PACKAGE_NAME
    docker container cp zakuro-$PACKAGE_NAME:/root/zakuro/_build/$PACKAGE_NAME/ $ZAKURO_DIR/_build/$PACKAGE_NAME
  popd
fi
