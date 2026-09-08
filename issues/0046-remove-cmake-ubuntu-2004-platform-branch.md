# CMakeLists.txt から Ubuntu 20.04 x86_64 の ZAKURO_PLATFORM 分岐を削除する

- Created: 2026-09-08
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-ubuntu-2004-branch
- Polished: {YYYY-MM-DD}

## 目的

`CHANGES.md 2025.1.0` で「Ubuntu 20.04 のビルドを削除」したが、`CMakeLists.txt` の
`ZAKURO_PLATFORM` 判定に `ubuntu-20.04_x86_64` の条件が残っており、削除漏れになっている。
`README.md` の動作環境 (macOS 15 arm64 / Ubuntu 22.04 x86_64 / Ubuntu 24.04 x86_64) と
CI (`.github/workflows/build.yml`) のビルド対象にも Ubuntu 20.04 は存在しない。

## 現状

`CMakeLists.txt` のリソース組み込み部分の `ZAKURO_PLATFORM` 判定は次のとおりで、
削除済みのはずの `ubuntu-20.04_x86_64` が残っている。

```
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
