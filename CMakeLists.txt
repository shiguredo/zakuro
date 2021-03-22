cmake_minimum_required(VERSION 3.16)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# MSVC runtime library flags are selected by an abstraction.
cmake_policy(SET CMP0091 NEW)

set(ZAKURO_PACKAGE_NAME "" CACHE STRING "ビルド設定を自動化するパッケージ名")
set(TARGET_OS "" CACHE STRING "ビルド対象の動作する OS。\n有効な値は macos, linux")
set(BOOST_ROOT_DIR "" CACHE PATH "Boost のインストール先ディレクトリ\n空文字だった場合はデフォルト検索パスの Boost を利用する")
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
  set(CLI11_ROOT_DIR ${_INSTALL_DIR}/CLI11)
  set(BLEND2D_ROOT_DIR ${_INSTALL_DIR}/blend2d)
  set(OPENH264_ROOT_DIR ${_INSTALL_DIR}/openh264)
  set(YAML_CPP_ROOT_DIR ${_INSTALL_DIR}/yaml-cpp)
  set(WEBRTC_INCLUDE_DIR ${_INSTALL_DIR}/webrtc/include)
  set(WEBRTC_LIBRARY_DIR ${_INSTALL_DIR}/webrtc/release/lib)
  #set(CLANG_ROOT ${_INSTALL_DIR}/llvm/clang)

  set(LIBCXX_INCLUDE_DIR ${_INSTALL_DIR}/llvm/libcxx/include)

elseif(ZAKURO_PACKAGE_NAME STREQUAL "ubuntu-18.04_x86_64")

  set(TARGET_OS "linux")
  set(TARGET_OS_LINUX "ubuntu-18.04")
  set(BOOST_ROOT_DIR /root/boost)
  set(CLI11_ROOT_DIR /root/CLI11)
  set(BLEND2D_ROOT_DIR /root/blend2d)
  set(OPENH264_ROOT_DIR /root/openh264)
  set(YAML_CPP_ROOT_DIR /root/yaml-cpp)
  set(WEBRTC_INCLUDE_DIR /root/webrtc/include)
  set(WEBRTC_LIBRARY_DIR /root/webrtc/lib)

  set(CLANG_ROOT /root/llvm/clang)
  set(LIBCXX_INCLUDE_DIR /root/llvm/libcxx/include)

elseif(ZAKURO_PACKAGE_NAME STREQUAL "ubuntu-20.04_x86_64")

  set(TARGET_OS "linux")
  set(TARGET_OS_LINUX "ubuntu-20.04")
  set(BOOST_ROOT_DIR /root/boost)
  set(CLI11_ROOT_DIR /root/CLI11)
  set(BLEND2D_ROOT_DIR /root/blend2d)
  set(OPENH264_ROOT_DIR /root/openh264)
  set(YAML_CPP_ROOT_DIR /root/yaml-cpp)
  set(WEBRTC_INCLUDE_DIR /root/webrtc/include)
  set(WEBRTC_LIBRARY_DIR /root/webrtc/lib)

  set(CLANG_ROOT /root/llvm/clang)
  set(LIBCXX_INCLUDE_DIR /root/llvm/libcxx/include)

else()
endif()

if (NOT "${CLANG_ROOT}" STREQUAL "")
  set(CMAKE_C_COMPILER ${CLANG_ROOT}/bin/clang)
  set(CMAKE_CXX_COMPILER ${CLANG_ROOT}/bin/clang++)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_PREFIX_PATH ${BOOST_ROOT_DIR} ${YAML_CPP_ROOT_DIR})

project(zakuro C CXX)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(WebRTC REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem json)
find_package(CLI11 REQUIRED)
find_package(Blend2D REQUIRED)
find_package(yaml-cpp REQUIRED)

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
    src/rtc/device_video_capturer.cpp
    src/rtc/dynamic_h264_video_encoder.cpp
    src/rtc/h264_format.cpp
    src/rtc/nop_video_decoder.cpp
    src/rtc/peer_connection_observer.cpp
    src/rtc/rtc_connection.cpp
    src/rtc/rtc_manager.cpp
    src/rtc/rtc_ssl_verifier.cpp
    src/rtc/scalable_track_source.cpp
    src/rtc/software_video_encoder.cpp
    src/rtc/zakuro_audio_device_module.cpp
    src/sora/sora_client.cpp
    src/sora/sora_server.cpp
    src/sora/sora_session.cpp
    src/ssl_verifier.cpp
    src/url_parts.cpp
    src/util.cpp
    src/virtual_client.cpp
    src/watchdog.cpp
    src/wav_reader.cpp
    src/websocket.cpp
    src/xorshift.cpp
    src/y4m_reader.cpp
    src/zakuro_version.cpp
    src/zakuro.cpp
)

target_include_directories(zakuro PRIVATE src)
set_target_properties(zakuro PROPERTIES CXX_STANDARD 14 C_STANDARD 99)

target_link_libraries(zakuro
  PRIVATE
    WebRTC::WebRTC
    Boost::filesystem
    Boost::json
    CLI11::CLI11
    Blend2D::Blend2D
    yaml-cpp
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

# リソースファイルを作る
list(APPEND RESOURCE_FILES
  Kosugi-Regular.ttf
  num000_01.wav
  num001_01.wav
  num002_01.wav
  num003_01.wav
  num004_01.wav
  num005_01.wav
  num006_01.wav
  num007_01.wav
  num008_01.wav
  num009_01.wav
  num010_01.wav
  num011_01.wav
  num012_01.wav
  num013_01.wav
  num014_01.wav
  num015_01.wav
  num016_01.wav
  num017_01.wav
  num018_01.wav
  num019_01.wav
  num020_01.wav
  num021_01.wav
  num022_01.wav
  num023_01.wav
  num024_01.wav
  num025_01.wav
  num026_01.wav
  num027_01.wav
  num028_01.wav
  num029_01.wav
  num030_01.wav
  num030_02.wav
  num040_01.wav
  num040_02.wav
  num050_01.wav
  num050_02.wav
  num060_01.wav
  num060_02.wav
  num070_01.wav
  num070_02.wav
  num080_01.wav
  num080_02.wav
  num090_01.wav
  num090_02.wav
)
list(LENGTH RESOURCE_FILES RESOURCE_FILES_LENGTH)
math(EXPR RESOURCE_FILES_LENGTH_MINUS_ONE "${RESOURCE_FILES_LENGTH} - 1")
foreach(n RANGE ${RESOURCE_FILES_LENGTH_MINUS_ONE})
  list(GET RESOURCE_FILES ${n} file)

  # ID として使う名前
  # Kosugi-Regular.ttf → RESOURCE_KOSUGI_REGULAR_TTF
  # num_030_2.wav → RESOURCE_NUM_030_2_WAV
  string(TOUPPER "RESOURCE_${file}" file_id)
  string(REGEX REPLACE "[-.]" "_" file_id "${file_id}")

  # macOS のセクション名(15文字以内)
  # Kosugi-Regular.ttf → Kosugi_Regular_
  # num_030_2.wav → num_030_2_wav
  string(REGEX REPLACE "[-.]" "_" file_name_macos "${file}")
  string(SUBSTRING "${file_name_macos}" 0 15 file_name_macos)

  # Linux の外部宣言の名前のベース
  # Kosugi-Regular.ttf → Kosugi_Regular_ttf
  # num_030_2.wav → num_030_2_wav
  string(REGEX REPLACE "[-.]" "_" file_name_linux "${file}")

  # バイナリの組み込み
  if (TARGET_OS STREQUAL "macos")
    target_link_options(zakuro PRIVATE "SHELL:-sectcreate __DATA ${file_name_macos} ${CMAKE_CURRENT_SOURCE_DIR}/resource/${file}")
  elseif (TARGET_OS STREQUAL "linux")
    set(objfile ${file}.o)
    add_custom_command(OUTPUT ${objfile}
      COMMAND ld
      ARGS -r -b binary -o ${CMAKE_CURRENT_BINARY_DIR}/${objfile} ${file}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resource
      MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/resource/${file})
    set_source_files_properties(${objfile}
      PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
    )
    target_sources(zakuro PRIVATE ${objfile})
  endif()

  string(APPEND RESOURCE_FILE_ID_DEFINES "#define ${file_id} ${n}\n")
  string(APPEND RESOURCE_FILE_NAME_MACOS_DEFINES "  \"${file_name_macos}\",\n")
  string(APPEND RESOURCE_FILE_NAME_LINUX_EXTERNS "extern const unsigned char _binary_${file_name_linux}_start[];\n")
  string(APPEND RESOURCE_FILE_NAME_LINUX_EXTERNS "extern const unsigned char _binary_${file_name_linux}_end[];\n")
  string(APPEND RESOURCE_FILE_NAME_LINUX_START_DEFINES "  _binary_${file_name_linux}_start,\n")
  string(APPEND RESOURCE_FILE_NAME_LINUX_END_DEFINES "  _binary_${file_name_linux}_end,\n")
endforeach()

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_binary.generated.template.h
  ${CMAKE_CURRENT_BINARY_DIR}/embedded_binary.generated.h
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_binary.generated.priv.template.h
  ${CMAKE_CURRENT_BINARY_DIR}/embedded_binary.generated.priv.h
  @ONLY)

if (TARGET_OS STREQUAL "macos")
  target_sources(zakuro
    PRIVATE
      src/mac_helper/mac_capturer.mm
      src/mac_helper/macos_version.mm
  )

  target_compile_options(zakuro PRIVATE -fconstant-string-class=NSConstantString)
  target_link_options(zakuro PRIVATE -ObjC)
  set_target_properties(zakuro PROPERTIES CXX_VISIBILITY_PRESET hidden)

  target_link_libraries(zakuro
    PRIVATE
      "-framework Foundation"
      "-framework AVFoundation"
      "-framework QuartzCore"
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
