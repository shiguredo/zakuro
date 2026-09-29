# CI で clang-format による整形チェックを実行する

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-ci-format-check
- Polished: {YYYY-MM-DD}
- Updated: 2026-09-29

## 目的

`.github/workflows/build.yml` はビルドとパッケージ作成のみを実行し、`src/` の整形を検証しない。
`.clang-format` が用意されているのに整形の崩れを CI で検出できないため、整形チェックを CI に組み込む。

## 現状

- `.github/workflows/build.yml` の `build_linux` / `build_macos` は `python3 run.py build <target> --package` のみを実行する
- `run.py format` は `clang-format` を PATH から探して `src/**/*.h` / `src/**/*.cpp` を `-i` で整形するが、
  チェック専用のモードが無く、CI から呼べる形になっていない
- `run.py build` が使う LLVM (`_install` 配下の llvm) には `clang-format` が含まれていない。
  CI の runner に入っているかは環境ごとに異なるため実装時に確認する
  (Ubuntu の runner image にはバージョン付きの `clang-format` が導入されている一方、
  macos-15 には無いという情報がある)
- `run.py format` の対象は `src/**/*.h` / `src/**/*.cpp` で、`test/` は対象外である。
  対象パターンは `run.py` にあり、`.clang-format` はスタイルのみを定義する
- `src/` は現状で整形違反が 1 件ある (`src/http_server.cpp` の `req.target() == "/.ok"` の条件式。
  2026-09-28 の revert で入った)
- CI への pytest 実行の組み込みは issue 0035 が対象であり、本 issue では扱わない
  (CTest は issues/0066 で撤去され、テストは実バイナリを起動する pytest に一本化された)

## 設計方針

- `clang-format` の導入方法を決める
  (runner にインストールする / 公式 action を使う / LLVM を固定バージョンで導入する)
  - バージョンによって整形結果が変わるため、CI と手元で同じ結果になるようバージョンを固定する。
    `.clang-format` ではバージョンを指定できないため、CI 側の導入方法で固定する
- チェック方法を決める
  (CI のステップから `clang-format --dry-run --Werror` を直接呼ぶ / `run.py` にチェック専用のオプションを足す)
  - `run.py format` は整形を書き換えるため、CI からは呼ばない
  - `format.sh` は別系統の整形経路で、`run.py format` への統合は issue 0044 が担当する。
    本 issue のチェック経路には含めない
- 整形チェックの対象は `src/` のみとする。issues/0066 で `test/` の C++ テスト実行ファイルが撤去され、
  `test/` に C/C++ のソースが無いため、`run.py format` の対象パターンの見直しは不要である
- 有効化の前に `src/` の整形違反を解消する

## 完了条件

- PR で `src/` の整形違反が CI で検出されること
- 既存の `src/` の整形違反 (現状は 1 件) が解消され、CI が通ること
- CI と手元で同じ `clang-format` のバージョンが使われ、判定が一致すること
  - 現状は手元が 23.1.0 で、Ubuntu の runner は別のバージョンのため、固定手段を決める必要がある
