# 未使用シンボル・関数・include の削除

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-unused-symbols-and-includes
- Polished: {YYYY-MM-DD}

## 目的

コードベース全体で参照されていない関数・宣言・include が多数残っている。まとめて削除する。

## 現状

以下は grep 検証で参照 0 件を確認。

### 未使用関数・宣言

- `src/util.h` / `src/util.cpp`: `Util::IceConnectionStateToString` は宣言・定義とも参照 0 件
- `src/util.h` / `src/util.cpp`: `Util::GenerateRandomChars` (両オーバーロード) は参照 0 件
- `src/util.h` / `src/util.cpp`: `Util::GenerateRandomNumericChars` は参照 0 件、内部で `std::rand()` を使う品質最悪の実装
- `src/game/game_audio.h`: `GameAudioManager::PlayAny` は参照 0 件、内部で `rand()` を使う
- `src/zakuro_audio_device_module.h`: `converted_audio_data_` メンバーは宣言のみで使われていない

### 未使用引数

- `src/zakuro.cpp` の `add_reconnect_scenario` ラムダの第 2 引数 `bool exit` は本体で参照 0 件。
  戻り値 `int op` も呼び出し元で捨てられている

### 未使用 include

- `src/zakuro.h`: `#include <boost/optional.hpp>` (使用箇所無し。std::optional に移行済み)
- `src/zakuro.cpp` L29: `#include "zakuro.h"` は L1 と重複
- `src/virtual_client.cpp`: `#include <iostream>` は `std::cerr` / `std::cout` を使っていないので不要
- `src/virtual_client.cpp`: builtin_audio_*_factory / webrtc_media_engine / audio_device / audio_device_factory / audio_processing の各 include は本 TU 内でシンボルを直接使っていない (Sora 側で使うので不要)
- `src/nop_video_decoder.cpp`: VP8 / VP9 / AV1 エンコーダーヘッダーの include は不要 (`CreateH264Format` のため `h264.h` だけ必要)

## 設計方針

- grep + iwyu (include-what-you-use) 相当のツールで参照 0 件の宣言・include を削除する
- 削除後にビルド・テストを回して回帰が無いことを確認する
- `Util::GenerateRandomNumericChars` は削除で対応 (品質の悪い `std::rand()` を放置しない)

## 完了条件

- 上記の未使用シンボル・宣言が全て削除されていること
- 上記の未使用 include が全て削除されていること
- macOS arm64 / Ubuntu 22.04 / Ubuntu 24.04 でビルドが通ること
- test_version が通ること
