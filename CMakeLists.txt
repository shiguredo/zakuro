cmake_minimum_required(VERSION 3.16)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(ZAKURO_PACKAGE_NAME "" CACHE STRING "ビルド設定を自動化するパッケージ名")
set(TARGET_OS "" CACHE STRING "ビルド対象の動作する OS。\n有効な値は macos, linux")
set(BOOST_ROOT_DIR "" CACHE PATH "Boost のインストール先ディレクトリ\n空文字だった場合はデフォルト検索パスの Boost を利用する")
set(JSON_ROOT_DIR "" CACHE PATH "nlohmann/json のインストール先ディレクトリ")
set(CLI11_ROOT_DIR "" CACHE PATH "CLI11 のインストール先ディレクトリ")
set(BLEND2D_ROOT_DIR "" CACHE PATH "Blend2D のインストール先ディレクトリ")
set(OPENH264_ROOT_DIR "" CACHE PATH "OpenH264 のインストール先ディレクトリ")
set(WEBRTC_INCLUDE_DIR "" CACHE PATH "WebRTC のインクルードディレクトリ")
set(WEBRTC_LIBRARY_DIR "" CACHE PATH "WebRTC のライブラリディレクトリ")
set(WEBRTC_LIBRARY_NAME "webrtc" CACHE STRING "WebRTC のライブラリ名")
set(CLANG_ROOT "" CACHE PATH "コンパイルに利用する clang コンパイラの配置場所")
set(USE_LIBCXX OFF CACHE BOOL "libstdc++ の代わりに libc++ を使うかどうか")
set(LIBCXX_INCLUDE_DIR "" CACHE PATH "libc++ を使う場合の libc++ のインクルードディレクトリ\n空文字だった場合はデフォルト検索パスの libc++ を利用する")
set(ZAKURO_VERSION "unspecified" CACHE STRING "Zakuro のバージョン")
set(ZAKURO_COMMIT "<commit>" CACHE STRING "Zakuro のコミットハッシュ")
set(WEBRTC_BUILD_VERSION "unspecified" CACHE STRING "webrtc-build のバージョン")
set(WEBRTC_READABLE_VERSION "unspecified" CACHE STRING "WebRTC の読みやすいバージョン")
set(WEBRTC_COMMIT "<commit>" CACHE STRING "WebRTC のコミットハッシュ")

if(ZAKURO_PACKAGE_NAME STREQUAL "macos")

  set(_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/macos/_install)

  set(TARGET_OS "macos")
  set(BOOST_ROOT_DIR ${_INSTALL_DIR}/boost)
  set(JSON_ROOT_DIR ${_INSTALL_DIR}/json)
  set(CLI11_ROOT_DIR ${_INSTALL_DIR}/CLI11)
  set(BLEND2D_ROOT_DIR ${_INSTALL_DIR}/blend2d)
  set(OPENH264_ROOT_DIR ${_INSTALL_DIR}/openh264)
  set(WEBRTC_INCLUDE_DIR ${_INSTALL_DIR}/webrtc/include)
  set(WEBRTC_LIBRARY_DIR ${_INSTALL_DIR}/webrtc/lib)
  set(CLANG_ROOT ${_INSTALL_DIR}/llvm/clang)

  set(LIBCXX_INCLUDE_DIR ${_INSTALL_DIR}/llvm/libcxx/include)

elseif(ZAKURO_PACKAGE_NAME STREQUAL "ubuntu-18.04_x86_64")

  set(TARGET_OS "linux")
  set(TARGET_OS_LINUX "ubuntu-18.04")
  set(BOOST_ROOT_DIR /root/boost)
  set(JSON_ROOT_DIR /root/json)
  set(CLI11_ROOT_DIR /root/CLI11)
  set(BLEND2D_ROOT_DIR /root/blend2d)
  set(OPENH264_ROOT_DIR /root/openh264)
  set(WEBRTC_INCLUDE_DIR /root/webrtc/include)
  set(WEBRTC_LIBRARY_DIR /root/webrtc/lib)

  set(CLANG_ROOT /root/llvm/clang)
  set(LIBCXX_INCLUDE_DIR /root/llvm/libcxx/include)

elseif(ZAKURO_PACKAGE_NAME STREQUAL "ubuntu-20.04_x86_64")

  set(TARGET_OS "linux")
  set(TARGET_OS_LINUX "ubuntu-20.04")
  set(BOOST_ROOT_DIR /root/boost)
  set(JSON_ROOT_DIR /root/json)
  set(CLI11_ROOT_DIR /root/CLI11)
  set(BLEND2D_ROOT_DIR /root/blend2d)
  set(OPENH264_ROOT_DIR /root/openh264)
  set(WEBRTC_INCLUDE_DIR /root/webrtc/include)
  set(WEBRTC_LIBRARY_DIR /root/webrtc/lib)

  set(CLANG_ROOT /root/llvm/clang)
  set(LIBCXX_INCLUDE_DIR /root/llvm/libcxx/include)

elseif(ZAKURO_PACKAGE_NAME STREQUAL "centos-8_x86_64")

  set(TARGET_OS "linux")
  set(TARGET_OS_LINUX "centos-8")
  set(BOOST_ROOT_DIR /root/boost)
  set(JSON_ROOT_DIR /root/json)
  set(CLI11_ROOT_DIR /root/CLI11)
  set(BLEND2D_ROOT_DIR /root/blend2d)
  set(OPENH264_ROOT_DIR /root/openh264)
  set(WEBRTC_INCLUDE_DIR /root/webrtc/include)
  set(WEBRTC_LIBRARY_DIR /root/webrtc/lib)

  set(CLANG_ROOT /root/llvm/clang)
  set(LIBCXX_INCLUDE_DIR /root/llvm/libcxx/include)

else()
endif()

set(CMAKE_C_COMPILER ${CLANG_ROOT}/bin/clang)
set(CMAKE_CXX_COMPILER ${CLANG_ROOT}/bin/clang++)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_PREFIX_PATH ${BOOST_ROOT_DIR})

project(zakuro C CXX)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(WebRTC REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(JSON REQUIRED)
find_package(CLI11 REQUIRED)
find_package(Blend2D REQUIRED)

add_executable(zakuro)

string(SUBSTRING "${ZAKURO_COMMIT}" 0 8 ZAKURO_COMMIT_SHORT)
string(SUBSTRING "${WEBRTC_COMMIT}" 0 8 WEBRTC_COMMIT_SHORT)
configure_file(src/zakuro_version.gen.h.template ${CMAKE_CURRENT_BINARY_DIR}/zakuro_version.gen.h)
target_include_directories(zakuro PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# OpenH264
target_include_directories(zakuro PRIVATE ${OPENH264_ROOT_DIR}/include)

target_sources(zakuro
  PRIVATE
    src/embedded_binary.cpp
    src/fake_video_capturer.cpp
    src/main.cpp
    src/url_parts.cpp
    src/xorshift.cpp
    src/virtual_client.cpp
    src/wav_reader.cpp
    src/y4m_reader.cpp
    src/rtc/device_video_capturer.cpp
    src/rtc/peer_connection_observer.cpp
    src/rtc/rtc_connection.cpp
    src/rtc/rtc_manager.cpp
    src/rtc/rtc_ssl_verifier.cpp
    src/rtc/scalable_track_source.cpp
    src/rtc/dynamic_h264_video_encoder.cpp
    src/rtc/nop_video_decoder.cpp
    src/rtc/software_video_encoder.cpp
    src/rtc/h264_format.cpp
    src/rtc/zakuro_audio_device_module.cpp
    src/sora/sora_client.cpp
    src/sora/sora_server.cpp
    src/sora/sora_session.cpp
    src/ssl_verifier.cpp
    src/util.cpp
    src/watchdog.cpp
    src/websocket.cpp
    src/zakuro_version.cpp
)

target_include_directories(zakuro PRIVATE src)
set_target_properties(zakuro PROPERTIES CXX_STANDARD 14 C_STANDARD 99)

target_link_libraries(zakuro
  PRIVATE
    WebRTC::WebRTC
    Boost::filesystem
    JSON::JSON
    CLI11::CLI11
    Blend2D::Blend2D
)

target_compile_definitions(zakuro
  PRIVATE
    OPENSSL_IS_BORINGSSL
)

target_compile_options(zakuro
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE_DIR}>"
)

if (TARGET_OS STREQUAL "macos")
  target_sources(zakuro
    PRIVATE
      src/mac_helper/mac_capturer.mm
      src/mac_helper/macos_version.mm
  )

  target_compile_options(zakuro PRIVATE -fconstant-string-class=NSConstantString)
  target_link_options(zakuro PRIVATE -ObjC)
  set_target_properties(zakuro PROPERTIES CXX_VISIBILITY_PRESET hidden)

  # バイナリの組み込み
  target_link_options(zakuro PRIVATE -sectcreate __DATA __kosugi_ttf ${CMAKE_CURRENT_SOURCE_DIR}/resource/Kosugi-Regular.ttf)

  target_link_libraries(zakuro
    PRIVATE
      "-framework Foundation"
      "-framework AVFoundation"
      "-framework CoreServices"
      "-framework CoreFoundation"
      "-framework AudioUnit"
      "-framework AudioToolbox"
      "-framework CoreAudio"
      "-framework CoreGraphics"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework VideoToolbox"
      "-framework AppKit"
      "-framework Metal"
      "-framework MetalKit"
      "-framework OpenGL"
  )

  target_compile_definitions(zakuro
    PRIVATE
      WEBRTC_POSIX
      WEBRTC_MAC
  )

elseif (TARGET_OS STREQUAL "linux")
  find_package(Threads REQUIRED)

  target_compile_definitions(zakuro
    PRIVATE
      WEBRTC_POSIX
      _LIBCPP_ABI_UNSTABLE
  )
  target_sources(zakuro
    PRIVATE
      src/v4l2_video_capturer/v4l2_video_capturer.cpp
  )

  # バイナリの組み込み
  add_custom_command(OUTPUT kosugi_ttf.o
    COMMAND ld
    ARGS -r -b binary -o ${CMAKE_CURRENT_BINARY_DIR}/kosugi_ttf.o Kosugi-Regular.ttf
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resource
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/resource/Kosugi-Regular.ttf)
  set_source_files_properties(kosugi_ttf.o
    PROPERTIES
      EXTERNAL_OBJECT true
      GENERATED true
  )
  target_sources(zakuro PRIVATE kosugi_ttf.o)

  set_target_properties(zakuro PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(zakuro
    PRIVATE
      X11
      Xau
      xcb
      plds4
      Xext
      expat
      dl
      nss3
      nssutil3
      plc4
      nspr4
      rt
      Threads::Threads
  )
endif()
