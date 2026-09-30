# CTest をやめて pytest に一本化する

- Created: 2026-09-29
- Completed: 2026-09-29
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

## 解決方法

2026-09-29 追記: CTest を撤去し、テストは pytest に一本化した。

- `CMakeLists.txt` から `enable_testing()`、`zakuro_adm_test` と `zakuro_game_key_core_test` の定義、
  `add_test`、`set_tests_properties` を削除した
- `test/zakuro_audio_device_module_test.cpp` と `test/game_key_core_test.cpp` を削除した
- `CHANGES.md` の `## develop` の `### misc` から、削除したテストの追加エントリ 2 件を削除した
- `src/` のコードは変更していない

検証:

- `python3 run.py build macos_arm64` が成功し、ビルド対象が `zakuro` だけになった
- `uv run pytest` が 20 passed / 1 skipped で、撤去前と同じ結果だった
- ビルドディレクトリの古い生成物 (`CTestTestfile.cmake` とテスト実行ファイル) を削除した状態で
  `ctest` を実行すると、テストが 1 件も登録されていないことを確認した

撤去で失う検証について:

- ADM の生成と破棄は pytest の E2E が起動のたびに通っている
  (`src/zakuro.cpp` の `configure_dependencies` は `no-audio-device` でも `kDummyAudio` の
  `ZakuroAudioDeviceModule` を生成して `dependencies.adm` に設定する)
- ADM の `Init` の再入と `GameKeyCore` の競合はプロセス内部の呼び出し条件であり、
  pytest では同じ形にできない。pytest での補い方はテスト拡充の issue (0043) で扱う
