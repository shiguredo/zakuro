# CI で clang-format による整形チェックを実行する

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-ci-format-check
- Polished: {YYYY-MM-DD}

## 目的

`.github/workflows/build.yml` はビルドとパッケージ作成のみを実行し、`src/` の整形を検証しない。
`.clang-format` が用意されているのに整形の崩れを CI で検出できないため、整形チェックを CI に組み込む。

## 現状

- `.github/workflows/build.yml` の `build_linux` / `build_macos` は `python3 run.py build <target> --package` のみを実行する
- `run.py format` は `clang-format` を PATH から探して `src/**/*.h` / `src/**/*.cpp` を `-i` で整形するが、
  チェック専用のモードが無く、CI から呼べる形になっていない
- CI の runner (ubuntu-22.04 / ubuntu-24.04 / macos-15) には `clang-format` が入っておらず、
  `run.py build` が使う LLVM (`_install` 配下の llvm) にも含まれていない
- `.clang-format` の対象は `src/**/*.h` / `src/**/*.cpp` であり、`test/` は `run.py format` の対象外である
- CI への pytest 実行の組み込みは issue 0035、CTest 実行の組み込みは issue 0043 が対象であり、
  本 issue では扱わない

## 設計方針

- `clang-format` の導入方法を決める
  (runner にインストールする / 公式 action を使う / LLVM を固定バージョンで導入する)
  - バージョンによって整形結果が変わるため、CI と手元で同じ結果になるようバージョンを固定する
- チェック方法を決める
  (CI のステップから `clang-format --dry-run --Werror` を直接呼ぶ / `run.py` にチェック専用のオプションを足す)
  - `run.py format` は整形を書き換えるため、CI からは呼ばない
- 対象を `src/` にするか `test/` も含めるかを決める。含める場合は `.clang-format` の対象パターンも見直す
- 既存の `src/` が違反 0 件であることを確認してから有効にする

## 完了条件

- PR で `src/` の整形違反が CI で検出されること
- 既存の `src/` が違反 0 件の状態で CI が通ること
- CI と手元で同じ `clang-format` のバージョンが使われ、判定が一致すること
