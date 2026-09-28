# CMake のテストターゲット定義が重複している

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-share-test-target-settings
- Polished: {YYYY-MM-DD}

## 目的

`CMakeLists.txt` の `zakuro_adm_test` と `zakuro_game_key_core_test` は、対象ソースと一部の設定を除いて
ほぼ同一の定義になっている。テストを足すたびに同じ定義がコピーされ、
片方だけ直してビルド条件がずれる事故につながるため共通化する。

## 現状

`CMakeLists.txt` の 2 つのテストターゲットは次の設定が重複している。

- `add_executable`
- `set_target_properties(... CXX_STANDARD 20 C_STANDARD 20)`
- `target_include_directories(... PRIVATE src)`
- `target_link_libraries(... PRIVATE Sora::sora)`
  (標準ライブラリの実装が libwebrtc.a に同梱された libc++ であるため、どちらも必要)
- `add_test`

ターゲットごとに異なる点は次のとおり。

- `zakuro_adm_test` は `src/zakuro_audio_device_module.cpp` を追加でビルドし、
  libwebrtc のヘッダをインクルードするため `OPENSSL_IS_BORINGSSL` / `BOOST_NO_CXX98_FUNCTION_BASE` と
  macOS 向けの `-fconstant-string-class=NSConstantString` / `-ObjC` / `CXX_VISIBILITY_PRESET hidden` を持つ
- `zakuro_game_key_core_test` は `Threads::Threads` を持ち、`set_tests_properties` で `TIMEOUT 60` を設定している

`enable_testing()` は `zakuro_adm_test` の定義の直前に 1 回だけ呼ばれている。

## 設計方針

- `zakuro_add_test(<name> <sources>...)` のような関数を `CMakeLists.txt` に定義し、両ターゲットをその関数で作る
- `Threads::Threads` を全テストに付けるか引数で切り替えるかを決め、決めた理由をコメントに残す
  (macOS では `-pthread` が付かないため実害は無く、Linux では `std::thread` を使うターゲットに必要)
- コンパイル定義と macOS 向けオプションを関数側に含めるか、libwebrtc のヘッダを使うターゲットだけに
  付けるかを決める
- `TIMEOUT` を関数側で揃えるか、ターゲットごとに設定するかを決める
  (`zakuro_adm_test` は実測 30 秒程度かかるため、揃える場合はその値も見直す)

## 完了条件

- 2 つのテストターゲットの定義の重複が解消されていること
- `ctest` が 2/2 通ること
- 各ターゲットのビルド条件 (標準・include・リンク・コンパイル定義・macOS 向けオプション) が
  変更前と等価であること
