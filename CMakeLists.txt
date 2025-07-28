cmake_minimum_required(VERSION 3.23)

# Only interpret if() arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)

project(zakuro C CXX)

list(APPEND CMAKE_PREFIX_PATH ${SORA_DIR} ${YAML_ROOT_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${SORA_DIR}/share/cmake)

set(Boost_USE_STATIC_LIBS ON)

set(WEBRTC_LIBRARY_NAME webrtc)
find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)
find_package(Sora REQUIRED)
find_package(Threads REQUIRED)
find_package(Blend2D REQUIRED)
find_package(CLI11 REQUIRED)
find_package(yaml-cpp REQUIRED)

# DuckDB を手動で設定（システムのライブラリを使わないため）
set(DuckDB_INCLUDE_DIRS ${DUCKDB_ROOT_DIR}/include)

# プラットフォーム別のダイナミックライブラリ設定
if(ZAKURO_PLATFORM STREQUAL "macos_arm64")
    # macOSの場合は .dylib
    set(DuckDB_LIBRARIES ${DUCKDB_ROOT_DIR}/lib/libduckdb.dylib)
elseif(ZAKURO_PLATFORM STREQUAL "ubuntu-22.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-24.04_x86_64")
    # Linuxの場合は .so
    set(DuckDB_LIBRARIES ${DUCKDB_ROOT_DIR}/lib/libduckdb.so)
else()
    message(FATAL_ERROR "Unsupported platform for DuckDB: ${ZAKURO_PLATFORM}")
endif()

add_executable(zakuro)

set_target_properties(zakuro PROPERTIES CXX_STANDARD 17 C_STANDARD 17)
set_target_properties(zakuro PROPERTIES POSITION_INDEPENDENT_CODE ON)

# RPATH設定
set_target_properties(zakuro PROPERTIES BUILD_RPATH_USE_ORIGIN ON)

string(SUBSTRING "${ZAKURO_COMMIT}" 0 8 ZAKURO_COMMIT_SHORT)
string(SUBSTRING "${WEBRTC_COMMIT}" 0 8 WEBRTC_COMMIT_SHORT)
configure_file(src/zakuro_version.gen.h.template ${CMAKE_CURRENT_BINARY_DIR}/zakuro_version.gen.h)
target_include_directories(zakuro PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# OpenH264
target_include_directories(zakuro PRIVATE ${OPENH264_ROOT_DIR}/include)
# DuckDB
target_include_directories(zakuro PRIVATE ${DuckDB_INCLUDE_DIRS})

target_sources(zakuro
  PRIVATE
    src/duckdb_stats_writer.cpp
    src/duckdb_utils.cpp
    src/embedded_binary.cpp
    src/fake_video_capturer.cpp
    src/http_server.cpp
    src/json_rpc.cpp
    src/main.cpp
    src/nop_video_decoder.cpp
    src/util.cpp
    src/virtual_client.cpp
    src/wav_reader.cpp
    src/xorshift.cpp
    src/y4m_reader.cpp
    src/zakuro.cpp
    src/zakuro_audio_device_module.cpp
    src/zakuro_version.cpp
)


target_link_libraries(zakuro
  PRIVATE
    Sora::sora
    Boost::filesystem
    CLI11::CLI11
    Blend2D::Blend2D
    yaml-cpp::yaml-cpp
    ${DuckDB_LIBRARIES}
)


# Ubuntu 24.04 での追加のリンクオプション
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(zakuro PRIVATE pthread dl)
endif()

target_compile_definitions(zakuro
  PRIVATE
    # CLI11 は C++17 でのビルドにおいて std::filesystem を利用するが、これを clang で解決するには c++fs のリンクが必要になる。
    # しかし、 libwebrtc の libc++ では std::filesystem が提供されていないため下記のオプションを追加して利用しないようにする。 
    CLI11_HAS_FILESYSTEM=0
    OPENSSL_IS_BORINGSSL
    # https://github.com/boostorg/container_hash/issues/22 と同じ問題が clang-15 でも起きるので、これを手動で定義して回避する
    BOOST_NO_CXX98_FUNCTION_BASE
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
  if (ZAKURO_PLATFORM STREQUAL "macos_arm64")
    target_link_options(zakuro PRIVATE "SHELL:-sectcreate __DATA ${file_name_macos} ${CMAKE_CURRENT_SOURCE_DIR}/resource/${file}")
  elseif (ZAKURO_PLATFORM STREQUAL "ubuntu-20.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-22.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-24.04_x86_64")
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

if (ZAKURO_PLATFORM STREQUAL "macos_arm64")
  target_compile_options(zakuro PRIVATE -fconstant-string-class=NSConstantString)
  target_link_options(zakuro PRIVATE -ObjC)
  set_target_properties(zakuro PROPERTIES CXX_VISIBILITY_PRESET hidden)
  # DuckDBダイナミックライブラリのRPATH設定
  set_target_properties(zakuro PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    INSTALL_RPATH "@executable_path"
    BUILD_WITH_INSTALL_RPATH TRUE
  )

elseif (ZAKURO_PLATFORM STREQUAL "ubuntu-20.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-22.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-24.04_x86_64")
  # LinuxでのRPATH設定
  set_target_properties(zakuro PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE
  )
endif()
