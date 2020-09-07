# syntax = docker/dockerfile:1.1.1-experimental
FROM centos:8

ARG PACKAGE_NAME

LABEL jp.shiguredo.zakuro=$PACKAGE_NAME

# パッケージのインストール

COPY script/yum_install_x86_64.sh /root/
RUN /root/yum_install_x86_64.sh

# WebRTC の取得

ARG WEBRTC_BUILD_VERSION

COPY script/get_webrtc.sh /root/
RUN /root/get_webrtc.sh "$WEBRTC_BUILD_VERSION" centos-8_x86_64 /root /root
#COPY webrtc/ /root/webrtc/

# コンパイラの取得

COPY script/get_llvm.sh /root/
RUN /root/get_llvm.sh /root/webrtc /root

# Boost のビルド

ARG BOOST_VERSION

COPY _cache/boost/ /root/_cache/boost/
COPY script/setup_boost.sh /root/
RUN \
  set -ex \
  && /root/setup_boost.sh "$BOOST_VERSION" /root/boost-source /root/_cache/boost \
  && cd /root/boost-source/source \
  && echo 'using clang : : /root/llvm/clang/bin/clang++ : ;' > project-config.jam \
  && ./b2 \
    cxxflags=' \
      -D_LIBCPP_ABI_UNSTABLE \
      -nostdinc++ \
      -isystem/root/llvm/libcxx/include \
    ' \
    linkflags=' \
    ' \
    toolset=clang \
    visibility=global \
    target-os=linux \
    address-model=64 \
    link=static \
    variant=release \
    install \
    -j`nproc` \
    --ignore-site-config \
    --prefix=/root/boost \
    --with-filesystem

# nlohmann/json の取得

ARG JSON_VERSION
RUN git clone --branch v$JSON_VERSION --depth 1 https://github.com/nlohmann/json.git /root/json

# CLI11 の取得

ARG CLI11_VERSION
RUN git clone --branch v$CLI11_VERSION --depth 1 https://github.com/CLIUtils/CLI11.git /root/CLI11

# CMake のインストール
ARG CMAKE_VERSION
COPY script/get_cmake.sh /root/
RUN /root/get_cmake.sh "$CMAKE_VERSION" Linux /root
ENV PATH "/root/cmake/bin:$PATH"

ARG BLEND2D_VERSION
ARG ASMJIT_VERSION
RUN cd /root \
  && git clone https://github.com/blend2d/blend2d blend2d_source \
  && mkdir blend2d_source/3rdparty \
  && git clone https://github.com/asmjit/asmjit blend2d_source/3rdparty/asmjit \
  && cd blend2d_source \
  && git reset --hard $BLEND2D_VERSION \
  && cd 3rdparty/asmjit \
  && git reset --hard $ASMJIT_VERSION \
  && mkdir /root/blend2d_build \
  && cd /root/blend2d_build \
  && cmake /root/blend2d_source \
      -DCMAKE_C_COMPILER=/root/llvm/clang/bin/clang \
      -DCMAKE_CXX_COMPILER=/root/llvm/clang/bin/clang++ \
      -DCMAKE_CXX_FLAGS="-D_LIBCPP_ABI_UNSTABLE -nostdinc++ -isystem/root/llvm/libcxx/include" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/root/blend2d \
      -DBLEND2D_STATIC=ON \
  && cmake --build . \
  && cmake --build . --target install \
  && rm -rf /root/blend2d_build \
  && rm -rf /root/blend2d_source

# OpenH264
ARG OPENH264_VERSION
RUN \
  set -ex \
  && cd /root \
  && git clone https://github.com/cisco/openh264.git openh264-source \
  && cd openh264-source \
  && git reset --hard v$OPENH264_VERSION \
  && make PREFIX=/root/openh264 install-headers \
  && rm -rf /root/openh264-source
