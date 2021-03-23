#!/bin/bash

cd "`dirname $0`"

# 引数の処理

PROGRAM="$0"

_PACKAGES=" \
  macos \
  ubuntu-18.04_x86_64 \
  ubuntu-20.04_x86_64 \
"

function show_help() {
  echo "$PROGRAM [--clean] [--package] [--no-cache] [--no-tty] [--no-mount] <package>"
  echo "<package>:"
  for package in $_PACKAGES; do
    echo "  - $package"
  done
}

PACKAGE=""
FLAG_CLEAN=0
FLAG_PACKAGE=0
DOCKER_BUILD_FLAGS=""
DOCKER_MOUNT_TYPE=mount

while [ $# -ne 0 ]; do
  case "$1" in
    "--clean" ) FLAG_CLEAN=1 ;;
    "--package" ) FLAG_PACKAGE=1 ;;
    "--no-cache" ) DOCKER_BUILD_FLAGS="$DOCKER_BUILD_FLAGS --no-cache" ;;
    "--no-tty" ) DOCKER_BUILD_FLAGS="$DOCKER_BUILD_FLAGS --progress=plain" ;;
    "--no-mount" ) DOCKER_MOUNT_TYPE=nomount ;;
    --* )
      show_help
      exit 1
      ;;
    * )
      if [ -n "$PACKAGE" ]; then
        show_help
        exit 1
      fi
      PACKAGE="$1"
      ;;
  esac
  shift 1
done

_FOUND=0
for package in $_PACKAGES; do
  if [ "$PACKAGE" = "$package" ]; then
    _FOUND=1
    break
  fi
done

if [ $_FOUND -eq 0 ]; then
  show_help
  exit 1
fi

echo "--clean: " $FLAG_CLEAN
echo "--package: " $FLAG_PACKAGE
echo "<package>: " $PACKAGE

set -ex

pushd ..
  ZAKURO_COMMIT="`git rev-parse HEAD`"
  ZAKURO_COMMIT_SHORT="`cat $ZAKURO_COMMIT | cut -b 1-8`"
popd

source ../VERSION

case "$PACKAGE" in
  "windows" )
    echo "Windows では build.bat を利用してください。"
    exit 1
    ;;
  "macos" )
    if [ $FLAG_CLEAN -eq 1 ]; then
      rm -rf ../_build/macos
      rm -rf macos/_source
      rm -rf macos/_build
      rm -rf macos/_install
      exit 0
    fi

    ./macos/install_deps.sh

    source ./macos/_install/webrtc/release/VERSIONS

    if [ -z "$JOBS" ]; then
      JOBS=`sysctl -n hw.logicalcpu_max`
      if [ -z "$JOBS" ]; then
        JOBS=1
      fi
    fi

    mkdir -p ../_build/$PACKAGE
    pushd ../_build/$PACKAGE
      cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DZAKURO_PACKAGE_NAME="macos" \
        -DZAKURO_VERSION="$ZAKURO_VERSION" \
        -DZAKURO_COMMIT="$ZAKURO_COMMIT" \
        -DWEBRTC_BUILD_VERSION="$WEBRTC_BUILD_VERSION" \
        -DWEBRTC_READABLE_VERSION="$WEBRTC_READABLE_VERSION" \
        -DWEBRTC_COMMIT="$WEBRTC_COMMIT" \
        ../..
      cmake --build . -j$JOBS
      cp ../../build/macos/_install/webrtc/NOTICE ./
    popd

    if [ $FLAG_PACKAGE -eq 1 ]; then
      MACOS_VERSION=`sw_vers -productVersion | cut -d '.' -f-2`

      pushd ..
        # パッケージのバイナリを作る
        rm -rf _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}
        rm -f _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}.tar.gz
        mkdir -p _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}
        cp    _build/macos/zakuro _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/
        cp    LICENSE             _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/
        cp    NOTICE              _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/
        cat   _build/macos/NOTICE >> _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/NOTICE
        curl -fLO http://www.openh264.org/BINARY_LICENSE.txt
        echo  "# OpenH264"        >> _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/NOTICE
        echo  '```'               >> _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/NOTICE
        cat   BINARY_LICENSE.txt  >> _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/NOTICE
        echo  '```'               >> _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}/NOTICE
        rm    BINARY_LICENSE.txt
        pushd _package
          tar czf zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}.tar.gz zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}
        popd

        rm -rf _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}
        echo ""
        echo "パッケージが _package/zakuro-${ZAKURO_VERSION}_macos-${MACOS_VERSION}.tar.gz に生成されました。"
      popd
    fi

    ;;
  * )
    if [ $FLAG_CLEAN -eq 1 ]; then
      rm -rf ../_build/$PACKAGE
      IMAGES="`docker image ls -q zakuro/$PACKAGE`"
      if [ -n "$IMAGES" ]; then
        docker image rm $IMAGES
      fi
      docker builder prune -f --filter=label=jp.shiguredo.zakuro=$PACKAGE
      exit 0
    fi

    rm -rf $PACKAGE/script
    cp -r ../script $PACKAGE/script

    # 可能な限りキャッシュを利用する
    mkdir -p $PACKAGE/_cache/boost/
    if [ -e ../_cache/boost/ ]; then
      cp -r ../_cache/boost/* $PACKAGE/_cache/boost/
    fi

    DOCKER_BUILDKIT=1 docker build \
      -t zakuro/$PACKAGE:m$WEBRTC_BUILD_VERSION \
      $DOCKER_BUILD_FLAGS \
      --build-arg WEBRTC_BUILD_VERSION=$WEBRTC_BUILD_VERSION \
      --build-arg BOOST_VERSION=$BOOST_VERSION \
      --build-arg SDL2_VERSION=$SDL2_VERSION \
      --build-arg SDL2_IMAGE_VERSION=$SDL2_IMAGE_VERSION \
      --build-arg CLI11_VERSION=$CLI11_VERSION \
      --build-arg CMAKE_VERSION=$CMAKE_VERSION \
      --build-arg BLEND2D_VERSION=$BLEND2D_VERSION \
      --build-arg ASMJIT_VERSION=$ASMJIT_VERSION \
      --build-arg OPENH264_VERSION=$OPENH264_VERSION \
      --build-arg YAML_CPP_VERSION=$YAML_CPP_VERSION \
      --build-arg PACKAGE_NAME=$PACKAGE \
      $PACKAGE

    rm -rf $PACKAGE/_cache/boost/

    # キャッシュしたデータを取り出す
    set +e
    docker container create -it --name zakuro-$PACKAGE zakuro/$PACKAGE:m$WEBRTC_BUILD_VERSION
    docker container start zakuro-$PACKAGE
    mkdir -p ../_cache/boost/
    docker container cp zakuro-$PACKAGE:/root/_cache/boost/. ../_cache/boost/
    docker container stop zakuro-$PACKAGE
    docker container rm zakuro-$PACKAGE
    set -e

    rm -r $PACKAGE/script

    ../script/docker_run.sh `pwd` `pwd`/.. $DOCKER_MOUNT_TYPE $PACKAGE zakuro/$PACKAGE:m$WEBRTC_BUILD_VERSION $ZAKURO_COMMIT

    if [ $FLAG_PACKAGE -eq 1 ]; then
      pushd ..
        rm -rf _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}
        rm -f _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}.tar.gz
        mkdir -p _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}
        cp    _build/${PACKAGE}/zakuro _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/
        cp    LICENSE                  _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/
        cp    NOTICE                   _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/
        cat   _build/${PACKAGE}/NOTICE >> _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/NOTICE
        curl -fLO http://www.openh264.org/BINARY_LICENSE.txt
        echo  "# OpenH264"             >> _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/NOTICE
        echo  '```'                    >> _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/NOTICE
        cat   BINARY_LICENSE.txt       >> _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/NOTICE
        echo  '```'                    >> _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}/NOTICE
        rm    BINARY_LICENSE.txt
        pushd _package
          tar czf zakuro-${ZAKURO_VERSION}_${PACKAGE}.tar.gz zakuro-${ZAKURO_VERSION}_${PACKAGE}
        popd

        rm -rf _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}
        echo ""
        echo "パッケージが _package/zakuro-${ZAKURO_VERSION}_${PACKAGE}.tar.gz に生成されました。"
      popd
    fi
    ;;
esac
