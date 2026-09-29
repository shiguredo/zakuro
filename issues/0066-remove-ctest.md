# CTest をやめて pytest に一本化する

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-ctest
- Polished: {YYYY-MM-DD}

## 目的

テストの実行方法を pytest に一本化する。CMake の `add_test` と CTest は issue 0004 の対応で導入されたが、
CI に組み込まれておらず (`.github/workflows/build.yml` は `python3 run.py build` のみを実行する)、
テスト実行ファイルはビルドされるだけで一度も実行されていない。
プロジェクトのテストは実バイナリを起動する pytest (`test/`) で行う方針とし、CTest 側を撤去する。

## 現状

- `CMakeLists.txt` に `enable_testing()`、`add_test(NAME zakuro_adm_test ...)`、
  `add_test(NAME zakuro_game_key_core_test ...)`、`set_tests_properties(zakuro_game_key_core_test ... TIMEOUT 60)` がある
- テスト実行ファイルは `test/zakuro_audio_device_module_test.cpp` (issue 0004 で追加) と
  `test/game_key_core_test.cpp` (issue 0006 の修正で追加) の 2 本で、いずれもテストフレームワークを使わず
  終了コードで合否を返す
- `test/` には pytest の E2E (`test_zakuro.py` / `test_client_cert.py` / `zakuro.py` / `conftest.py`) があり、
  `uv run pytest` で 20 passed / 1 skipped になる
- `run.py` に `test` サブコマンドが無いため、CI からテストを実行する手段が無い

## 設計方針

- `CMakeLists.txt` から `enable_testing()` と 2 つのテストターゲットの定義、`add_test`、
  `set_tests_properties` を削除する
- `test/zakuro_audio_device_module_test.cpp` と `test/game_key_core_test.cpp` を削除する
- `CHANGES.md` の `## develop` の `### misc` から、削除するテストの追加エントリ 2 件を削除する
- `src/` のコードは変更しない
- 撤去で失う検証を pytest でどう補うかはテスト拡充の issue (0043) で扱う。本 issue では撤去だけを行う

## 完了条件

- `CMakeLists.txt` に `enable_testing()` / `add_test` / `set_tests_properties` が残っていないこと
- `test/` に C++ のテスト実行ファイル (`zakuro_audio_device_module_test.cpp` / `game_key_core_test.cpp`) が残っていないこと
- `CHANGES.md` に削除したテストの追加エントリが残っていないこと
- `python3 run.py build macos_arm64` が成功すること
- `uv run pytest` の結果が撤去前と同じ (20 passed / 1 skipped) であること
