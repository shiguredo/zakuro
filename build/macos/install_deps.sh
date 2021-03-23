#!/bin/bash

cd "`dirname $0`"

set -ex

SOURCE_DIR="`pwd`/_source"
BUILD_DIR="`pwd`/_build"
INSTALL_DIR="`pwd`/_install"
CACHE_DIR="`pwd`/../../_cache"

mkdir -p $SOURCE_DIR
mkdir -p $BUILD_DIR
mkdir -p $INSTALL_DIR
mkdir -p $CACHE_DIR

source ../../VERSION

if [ -z "$JOBS" ]; then
  JOBS=`sysctl -n hw.logicalcpu_max`
  if [ -z "$JOBS" ]; then
    JOBS=1
  fi
fi

# CLI11
CLI11_VERSION_FILE="$INSTALL_DIR/cli11.version"
CLI11_CHANGED=0
if [ ! -e $CLI11_VERSION_FILE -o "$CLI11_VERSION" != "`cat $CLI11_VERSION_FILE`" ]; then
  CLI11_CHANGED=1
fi

if [ $CLI11_CHANGED -eq 1 -o ! -e $INSTALL_DIR/CLI11/include/CLI/Version.hpp ]; then
  pushd $INSTALL_DIR
    rm -rf CLI11
    git clone --branch v$CLI11_VERSION --depth 1 https://github.com/CLIUtils/CLI11.git
  popd
fi
echo $CLI11_VERSION > $CLI11_VERSION_FILE

# WebRTC
WEBRTC_VERSION_FILE="$INSTALL_DIR/webrtc.version"
WEBRTC_CHANGED=0
if [ ! -e $WEBRTC_VERSION_FILE -o "$WEBRTC_BUILD_VERSION" != "`cat $WEBRTC_VERSION_FILE`" ]; then
  WEBRTC_CHANGED=1
fi

if [ $WEBRTC_CHANGED -eq 1 -o ! -e $INSTALL_DIR/webrtc/release/lib/libwebrtc.a ]; then
  rm -rf $INSTALL_DIR/webrtc
  ../../script/get_webrtc.sh $WEBRTC_BUILD_VERSION macos_`uname -m` $INSTALL_DIR $SOURCE_DIR
fi
echo $WEBRTC_BUILD_VERSION > $WEBRTC_VERSION_FILE

# LLVM
if [ ! -e $INSTALL_DIR/llvm/clang/bin/clang++ ]; then
  rm -rf $INSTALL_DIR/llvm
  ../../script/get_llvm.sh $INSTALL_DIR/webrtc $INSTALL_DIR
fi

# Boost
BOOST_VERSION_FILE="$INSTALL_DIR/boost.version"
BOOST_CHANGED=0
if [ ! -e $BOOST_VERSION_FILE -o "$BOOST_VERSION" != "`cat $BOOST_VERSION_FILE`" ]; then
  BOOST_CHANGED=1
fi

if [ $BOOST_CHANGED -eq 1 -o ! -e $INSTALL_DIR/boost/lib/libboost_filesystem.a ]; then
  rm -rf $SOURCE_DIR/boost
  rm -rf $BUILD_DIR/boost
  rm -rf $INSTALL_DIR/boost
  ../../script/setup_boost.sh $BOOST_VERSION $SOURCE_DIR/boost $CACHE_DIR/boost
  pushd $SOURCE_DIR/boost/source
    echo "using clang : : clang++ : ;" > project-config.jam
    SYSROOT="`xcrun --sdk macosx --show-sdk-path`"
    ./b2 \
      cflags=" \
        --sysroot=$SYSROOT \
      " \
      cxxflags=" \
        --sysroot=$SYSROOT \
        -std=c++14 \
      " \
      toolset=clang \
      visibility=hidden \
      link=static \
      variant=release \
      install \
      -j$JOBS \
      --build-dir=$BUILD_DIR/boost \
      --prefix=$INSTALL_DIR/boost \
      --ignore-site-config \
      --with-filesystem \
      --with-json
  popd
fi
echo $BOOST_VERSION > $BOOST_VERSION_FILE

# Blend2D
BLEND2D_VERSION_FILE="$INSTALL_DIR/blend2d.version"
BLEND2D_CHANGED=0
if [ ! -e $BLEND2D_VERSION_FILE -o "$BLEND2D_VERSION,$ASMJIT_VERSION" != "`cat $BLEND2D_VERSION_FILE`" ]; then
  BLEND2D_CHANGED=1
fi
if [ $BLEND2D_CHANGED -eq 1 -o ! -e $INSTALL_DIR/blend2d/lib/libblend2d.a ]; then
  rm -rf $SOURCE_DIR/blend2d
  rm -rf $BUILD_DIR/blend2d
  rm -rf $INSTALL_DIR/blend2d

  git clone https://github.com/blend2d/blend2d $SOURCE_DIR/blend2d
  pushd $SOURCE_DIR/blend2d
    git reset --hard $BLEND2D_VERSION
  popd

  mkdir $SOURCE_DIR/blend2d/3rdparty
  git clone https://github.com/asmjit/asmjit $SOURCE_DIR/blend2d/3rdparty/asmjit
  pushd $SOURCE_DIR/blend2d/3rdparty/asmjit
    git reset --hard $ASMJIT_VERSION
  popd

  mkdir $BUILD_DIR/blend2d
  pushd $BUILD_DIR/blend2d
    sed -i -e 's/\(blend2d_detect_cflags.*mavx.*\)/#\1/' $SOURCE_DIR/blend2d/CMakeLists.txt
    cmake $SOURCE_DIR/blend2d \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR/blend2d \
      -DBLEND2D_STATIC=ON \
      -DBLEND2D_NO_JIT=ON
    cmake --build .
    cmake --build . --target install
  popd
fi
echo "$BLEND2D_VERSION,$ASMJIT_VERSION" > $BLEND2D_VERSION_FILE

# OpenH264
# バイナリは実行時にパスを渡すので、ヘッダーだけインストールする
OPENH264_VERSION_FILE="$INSTALL_DIR/openh264.version"
OPENH264_CHANGED=0
if [ ! -e $OPENH264_VERSION_FILE -o "$OPENH264_VERSION" != "`cat $OPENH264_VERSION_FILE`" ]; then
  OPENH264_CHANGED=1
fi
if [ $OPENH264_CHANGED -eq 1 -o ! -e $INSTALL_DIR/openh264/include/wels/codec_api.h ]; then
  rm -rf $SOURCE_DIR/openh264
  rm -rf $INSTALL_DIR/openh264

  git clone https://github.com/cisco/openh264.git $SOURCE_DIR/openh264
  pushd $SOURCE_DIR/openh264
    git reset --hard v$OPENH264_VERSION
    make PREFIX=$INSTALL_DIR/openh264 install-headers
  popd
fi
echo "$OPENH264_VERSION" > $OPENH264_VERSION_FILE

# yaml-cpp
YAML_CPP_VERSION_FILE="$INSTALL_DIR/yaml-cpp.version"
YAML_CPP_CHANGED=0
if [ ! -e $YAML_CPP_VERSION_FILE -o "$YAML_CPP_VERSION" != "`cat $YAML_CPP_VERSION_FILE`" ]; then
  YAML_CPP_CHANGED=1
fi
if [ $YAML_CPP_CHANGED -eq 1 -o ! -e $INSTALL_DIR/yaml-cpp/lib/libyaml-cpp.a ]; then
  rm -rf $SOURCE_DIR/yaml-cpp
  rm -rf $INSTALL_DIR/yaml-cpp
  rm -rf $BUILD_DIR/yaml-cpp

  pushd $SOURCE_DIR
    git clone --branch yaml-cpp-$YAML_CPP_VERSION --depth 1 https://github.com/jbeder/yaml-cpp.git
  popd

  mkdir -p $BUILD_DIR/yaml-cpp
  pushd $BUILD_DIR/yaml-cpp
    cmake $SOURCE_DIR/yaml-cpp \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR/yaml-cpp \
      -DYAML_CPP_BUILD_TESTS=OFF \
      -DYAML_CPP_BUILD_TOOLS=OFF
    cmake --build .
    cmake --build . --target install
  popd
fi
echo "$YAML_CPP_VERSION" > $YAML_CPP_VERSION_FILE
