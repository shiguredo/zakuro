# CMakeLists.txt から Ubuntu 20.04 x86_64 の ZAKURO_PLATFORM 分岐を削除する

- Created: 2026-09-08
- Completed: 2026-09-30
- Branch: feature/remove-ubuntu-2004-branch
- Polished: 2026-09-08

## 目的

`CHANGES.md 2025.1.0` で「Ubuntu 20.04 のビルドを削除」したが、`CMakeLists.txt` の
`ZAKURO_PLATFORM` 判定に `ubuntu-20.04_x86_64` の条件が残っており、削除漏れになっている。
`README.md` の動作環境 (macOS 15 arm64 / Ubuntu 22.04 x86_64 / Ubuntu 24.04 x86_64) と
CI (`.github/workflows/build.yml`) のビルド対象にも Ubuntu 20.04 は存在しない。

## 現状

`CMakeLists.txt` のリソース組み込み部分の `ZAKURO_PLATFORM` 判定は次のとおりで、
削除済みのはずの `ubuntu-20.04_x86_64` が残っている。

```cmake
elseif (ZAKURO_PLATFORM STREQUAL "ubuntu-20.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-22.04_x86_64" OR ZAKURO_PLATFORM STREQUAL "ubuntu-24.04_x86_64")
```

`CHANGES.md 2025.1.0` には「Ubuntu 20.04 のビルドを削除」とあり、README と CI に Ubuntu 20.04 が
無いのに CMakeLists.txt だけが残っている状態。

## 設計方針

- `CMakeLists.txt` の当該 `elseif` 条件から `ZAKURO_PLATFORM STREQUAL "ubuntu-20.04_x86_64"` の OR を削除する
- `ubuntu-22.04_x86_64` / `ubuntu-24.04_x86_64` の条件は残す
- `CMakeLists.txt` には body が空の `elseif` にも `ubuntu-20.04_x86_64` が残っているが、
  そちらは issues/0036 (空の elseif ブロックの削除) の対応範囲とし、本 issue では扱わない
- `buildbase.py` の `ubuntu-20.04` (`get_webrtc_platform` の Jetson 分岐) は Jetson のベース OS の
  命名であり、本 issue の対象外とする

## 完了条件

- リソース組み込み部分の `ZAKURO_PLATFORM` 判定から `ubuntu-20.04_x86_64` が除去されていること
- ubuntu-22.04 / ubuntu-24.04 / macOS のビルドが通ること

## 解決方法

`CMakeLists.txt` の `ZAKURO_LINUX_PLATFORMS` から `ubuntu-20.04_x86_64` を削除した。

issue の「現状」が引用していた
`elseif (ZAKURO_PLATFORM STREQUAL "ubuntu-20.04_x86_64" OR ...)` の条件分岐は、
その後のリファクタリングで `ZAKURO_LINUX_PLATFORMS` の一覧を使う形に変わっており
現存しない。実際に残っていたのは一覧の中の 1 行で、`run.py` の
`LINUX_X86_64_PLATFORMS` / `LINUX_ARMV8_PLATFORMS` (22.04 / 24.04 / 26.04 /
26.04_armv8 の 4 つ) と一致していなかった。この 1 行を削除して一致させた。

検証したこと:

- `CMakeLists.txt` の `ZAKURO_LINUX_PLATFORMS` と `run.py` の `LINUX_PLATFORMS` が
  一致すること (22.04 / 24.04 / 26.04 x86_64 と 26.04 armv8)
- `python3 run.py build macos_arm64` が成功する
- `git grep -n 'ubuntu-20.04' CMakeLists.txt` の結果が 0 件になる
- ubuntu-22.04 / ubuntu-24.04 のビルドは CI の Build zakuro が検証する

`CHANGES.md` の `## develop` の `### misc` に `[UPDATE]` のエントリを追加した。
