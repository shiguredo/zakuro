# CMakeLists.txt 掃除 (空 elseif・古いコメント・警告フラグ・C_STANDARD 20)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-cmake-cleanup
- Polished: 2026-09-08

## 目的

`CMakeLists.txt` に以下の複数の掃除ポイントがある。まとめて対応する。

- Ubuntu 20.04 分岐が残っているのに body が空の elseif が残置
- `BOOST_NO_CXX98_FUNCTION_BASE` のコメントが clang-15 参照で古い
- `-Wall -Wextra` などの警告フラグが一切ない
- `set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)` の `C_STANDARD 20` は未定義値

## 現状

### 空の elseif

`CMakeLists.txt` の末尾のプラットフォーム判定で、`ZAKURO_PLATFORM` が
`ubuntu-20.04_x86_64` / `ubuntu-22.04_x86_64` / `ubuntu-24.04_x86_64` のときの `elseif` の body が空。

```
if (ZAKURO_PLATFORM STREQUAL "macos_arm64")
  target_compile_options(zakuro PRIVATE -fconstant-string-class=NSConstantString)
  target_link_options(zakuro PRIVATE -ObjC)
  set_target_properties(zakuro PROPERTIES CXX_VISIBILITY_PRESET hidden)

elseif (ZAKURO_PLATFORM STREQUAL "ubuntu-20.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-22.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-24.04_x86_64")
endif()
```

`if` 側は macOS 向けのフラグ設定を持つが、`elseif` 側は何も実行しないため、プラットフォーム分岐として
機能していない。`CHANGES.md 2025.1.0` で Ubuntu 20.04 のビルドを削除済みで、
リソース組み込み部分の `ZAKURO_PLATFORM` 判定に残る `ubuntu-20.04_x86_64` の削除は issues/0046 の範囲とし、
本 issue ではこの空の `elseif` ブロックごと削除する。

### `BOOST_NO_CXX98_FUNCTION_BASE` のコメント

`CMakeLists.txt` の該当コメント:

```
# https://github.com/boostorg/container_hash/issues/22 と同じ問題が clang-15 でも起きるので、これを手動で定義して回避する
```

このコメントは 2022-09 の m105 対応時に追加されたもので、libwebrtc がバンドルする clang は当時
clang-15 だった。DEPS の `WEBRTC_BUILD_VERSION` は m150 のため、現行のバンドル clang は
clang-20 前後と推定される (正確なバージョンはビルド時に `clang --version` で確認する)。
clang-15 参照のままなのは実態とずれている。

Boost 1.91 (DEPS の `BOOST_VERSION=1.91.0`) で本マクロ無しでビルドが通るなら削除、通らないなら
コメントを確認したバージョンに更新する。

### 警告フラグ無し

`-Wall -Wextra -Werror` などが未指定。UB サニタイザ・AddressSanitizer のビルドフラグも用意されていない。
`issues/` に登録されている UB / UAF 系バグ (例: issues/0004-bug-adm-init-reentry-uaf.md、
issues/0010-bug-file-rotating-log-sink-uaf.md) は sanitizer を有効にすれば再現・検知しやすくなる。

### C_STANDARD 20

`set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)` の `C_STANDARD 20` は
CMake が認識しない値。CMake の `C_STANDARD` が認識する値は 90 / 99 / 11 / 17 / 23 のみ (17 と 23 は
CMake 3.21 で追加)。不正な値のまま C ソースをコンパイルすると、CMake は generate 時に
`C_STANDARD is set to invalid value '20'` エラーで停止する。

現状リポジトリ (src/ を含む) に `.c` ソースは存在しないためこのエラーは発火せず、
コンパイラのデフォルト C 標準が使われる。しかし `.c` ファイルを追加した瞬間にビルドが壊れる。
意図が C11 なのか C17 なのか C23 なのか不明。

## 設計方針

- 空の `elseif` ブロックを削除する (macOS 向け `if` ブロックの設定は維持する。Ubuntu 20.04 分岐削除は
  issues/0046 と連動)
- Boost 1.91 で `BOOST_NO_CXX98_FUNCTION_BASE` 無しでビルドが通るか実測して判断
  - 通るなら定義削除、通らないならコメントを「実測した clang バージョンでも必要」に更新
- `-Wall -Wextra` をデフォルトで有効化する
- `option(ZAKURO_ENABLE_SANITIZER ...)` で `-fsanitize=address,undefined` を有効化するオプションを追加
  - コンパイルとリンクの両方に付与する (`target_compile_options` と `target_link_options`)
- `C_STANDARD 20` を `C_STANDARD 17` (または 23) に修正する

## 完了条件

- CMakeLists.txt に空の elseif ブロックが残っていないこと
- `-Wall -Wextra` が有効になっていること (build.yml の CI でも警告が出ないこと)
- `C_STANDARD` が CMake が認識する値になっていること
- サニタイザ有効ビルドが可能になっていること
