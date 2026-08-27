# CMakeLists.txt 掃除 (空 elseif・古いコメント・警告フラグ・C_STANDARD 20)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-cmake-cleanup
- Polished: {YYYY-MM-DD}

## 目的

`CMakeLists.txt` に以下の複数の掃除ポイントがある。まとめて対応する。

- Ubuntu 20.04 分岐が残っているのに body が空の elseif が残置
- `BOOST_NO_CXX98_FUNCTION_BASE` のコメントが clang-15 参照で古い
- `-Wall -Wextra` などの警告フラグが一切ない
- `set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)` の `C_STANDARD 20` は未定義値

## 現状

### 空の elseif

`CMakeLists.txt` の `ZAKURO_PLATFORM` 判定ブロックで、Ubuntu 22.04 / 24.04 向けの `elseif` の body が空。
Ubuntu 20.04 の削除 (CHANGES.md 2025.1.0) に伴う掃除漏れ。

### `BOOST_NO_CXX98_FUNCTION_BASE` のコメント

`CMakeLists.txt` の該当コメント:

```
# https://github.com/boostorg/container_hash/issues/22 と同じ問題が clang-15 でも起きるので、これを手動で定義して回避する
```

WEBRTC_BUILD_VERSION=m150 の libwebrtc がバンドルする clang は m141〜m150 世代で、clang-20 前後と推定される。
clang-15 参照は残置。Boost 1.91 で本マクロ無しでビルドが通るなら削除、通らないならコメントを更新する。

### 警告フラグ無し

`-Wall -Wextra -Werror` などが未指定。UB サニタイザ・AddressSanitizer のビルドフラグも用意されていない。
本レビューで挙げた UB / UAF は sanitizer を入れれば大半検知できる。

### C_STANDARD 20

`set_target_properties(zakuro PROPERTIES CXX_STANDARD 20 C_STANDARD 20)` の `C_STANDARD 20` は
CMake が認識しない値 (CMake は 90/99/11/17/23 のみ認識、CMake 3.28 以降で 23)。
警告なく無視され、コンパイラのデフォルト C 標準が使われる。意図が C11 なのか C17 なのか C23 なのか不明。

## 設計方針

- 空の `elseif` ブロックを削除する (Ubuntu 20.04 分岐削除の別 issue と連動)
- Boost 1.91 で `BOOST_NO_CXX98_FUNCTION_BASE` 無しでビルドが通るか実測して判断
  - 通るなら定義削除、通らないならコメントを「Boost 1.91 でも必要」に更新
- `-Wall -Wextra` をデフォルトで有効化する
- `option(ZAKURO_ENABLE_SANITIZER ...)` で `-fsanitize=address,undefined` を有効化するオプションを追加
- `C_STANDARD 20` を `C_STANDARD 17` (または 23) に修正

## 完了条件

- CMakeLists.txt に空の elseif ブロックが残っていないこと
- `-Wall -Wextra` が有効になっていること (build.yml の CI でも警告が出ないこと)
- `C_STANDARD` が CMake が認識する値になっていること
- サニタイザ有効ビルドが可能になっていること
