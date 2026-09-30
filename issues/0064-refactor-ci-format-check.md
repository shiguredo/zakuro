# CI で clang-format による整形チェックを実行する

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-ci-format-check
- Polished: 2026-09-30
- Updated: 2026-09-29

## 目的

検証用の `.github/workflows/ci.yml` はビルドと pytest の実行のみで、`src/` の整形を検証しない。
`.clang-format` が用意されているのに整形の崩れを CI で検出できないため、整形チェックを CI に組み込む。

## 現状

- 以前の `.github/workflows/build.yml` は issue 0035 で `ci.yml` / `release.yml` に分割され、削除された。
  分割後の `ci.yml` / `release.yml` のいずれにも `clang-format` による整形チェックは無い
- `ci.yml` の `build_linux` / `build_macos` は `python3 run.py build <target>` のみを実行し、
  `release.yml` は `--package` 付きで実行する。いずれも整形チェックを行わない
- `ci.yml` の `prek` ジョブは prek.toml (と test/prek.toml) のフックを実行するが、そこに clang-format のフックは無い。
  なお prek.toml では `.clang-format` が複数の YAML ドキュメントを持つため check-yaml の除外対象になっている
- `run.py format` は `clang-format` を PATH から探して `src/**/*.h` / `src/**/*.cpp` を `-i` で整形するが、
  チェック専用のモードが無く、CI から呼べる形になっていない
- `run.py build` が使う LLVM (`_install` 配下の llvm) に `clang-format` が含まれるかは未確認であり、
  実装時に確認する。CI の runner に入っているかも環境ごとに異なるため、実装時に確認する
  (Ubuntu の runner image にはバージョン付きの `clang-format` が導入されている一方、
  macos-15 には無いという情報がある)
- `run.py format` の対象は `src/**/*.h` / `src/**/*.cpp` で、`test/` は対象外である。
  対象パターンは `run.py` にあり、`.clang-format` はスタイルのみを定義する
- `src/` の整形違反は 0 件である。2026-09-28 の revert で入った `src/http_server.cpp` の
  `req.target() == "/.ok"` の条件式の違反は 2026-09-29 に修正済みであり、
  clang-format 23.1.0 の `--dry-run --Werror` が `src/` の全ファイルに対して通ることを確認済みである
- pytest は issue 0035 で `ci.yml` に組み込み済みであり、本 issue では扱わない
  (CTest は issue 0066 で撤去済みで、テストは実バイナリを起動する pytest に一本化されている)

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
- 整形チェックの対象は `src/` のみとする。issue 0066 で `test/` の C++ テスト実行ファイルが撤去され、
  `test/` に C/C++ のソースが無いため、`run.py format` の対象パターンの見直しは不要である
- 有効化の前に `src/` の整形違反が 0 件であることを確認する (現状は 0 件である)

## 完了条件

- PR で `src/` の整形違反が CI で検出されること
- 整形チェックが `src/` の全ファイルで 0 件で通ること (現状の整形違反は 0 件である)
- CI と手元で同じ `clang-format` のバージョンが使われ、判定が一致すること
  - 手元は 23.1.0 であり、runner の `clang-format` のバージョンが同一になるとは限らないため、
    固定手段を決める必要がある (runner 側は Ubuntu に導入されている、macos-15 には無いという情報がある)
