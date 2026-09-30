# boost::variant の std::variant 化と直接 new の make_unique / make_shared 置換

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-boost-legacy-and-make-unique
- Polished: 2026-09-08

## 目的

C++20 でビルドしているのに、`boost::variant` と直接 `new T` によるスマートポインタ生成が残っている。
標準ライブラリの `std::variant` / `std::make_unique` / `std::make_shared` に寄せてモダン化する。

## 現状

### boost::variant

- `src/scenario_player.h` の `ScenarioData::operation_t` が `boost::variant<...>` を使用している。
- 参照箇所は `src/scenario_player.h` の `OnNext` 内の `opv.which()` による switch 分岐と、その 3 つの case での `boost::get<T>(opv)` のみ。
- C++17 以降の `std::variant` で置換できるため、Boost 依存を 1 つ減らせる。

### 直接 new T (std::unique_ptr 系。std::make_unique で置換可能)

- `src/main.cpp`:
  - `log_sink` の初期化 (`std::unique_ptr<webrtc::FileRotatingLogSink>(new ...)`。改行をまたぐため grep では検出できない)
  - `stats_th` の `.reset(new std::thread(...))`
  - `ths` への `std::unique_ptr<std::thread>(new std::thread(...))` の push_back
  - `http_server` の `.reset(new HttpServer(...))`
- `src/http_server.cpp`: `thread_` と `acceptor_` (`boost::asio::ip::tcp::acceptor`) の `.reset(new ...)`
- `src/fake_video_capturer.cpp`: `thread_` の `.reset(new std::thread(...))`
- `src/fake_audio_key_trigger.h`: `th_` の `.reset(new std::thread(...))`
- `src/game/game_key_core.h`: `th_` の `.reset(new std::thread(...))`
- `src/game/game_audio.h`: `GameAudioManager::AddGameAudio` の `std::unique_ptr<GameAudio>(new GameAudio(...))`
- `src/binary_pool.h`: `bin_` (`std::unique_ptr<uint8_t[]>`) の `.reset(new uint8_t[size_])`。配列なので `std::make_unique<uint8_t[]>(size_)` になる
- `src/zakuro.cpp`: `gam` (GameAudioManager) と `trigger` (FakeAudioKeyTrigger) の `.reset(new ...)`
- `src/zakuro_audio_device_module.cpp`: `audio_thread_` の `.reset(new std::thread(...))`

### 直接 new T (std::shared_ptr 系。std::make_shared で置換可能)

- `src/main.cpp`: `key_core` (`std::shared_ptr<GameKeyCore>(new ...)`) と `stats` (`std::shared_ptr<ZakuroStats>(new ...)`)
- `src/scenario_player.h`: `ScenarioData::PlaySubScenario` の `std::shared_ptr<ScenarioData>(new ScenarioData(...))`
- `src/zakuro.cpp`: `vc_config.fake_audio` と `spc.binary_pool` の `.reset(new ...)` (いずれも `std::shared_ptr` 型)
- `src/zakuro_audio_device_module.cpp`: `fake_audio_` の `.reset(new ...)` (`std::shared_ptr` 型)

### 置換しない (例外)

- `src/virtual_client.cpp` の `VirtualClient::Create`: `VirtualClient` のコンストラクターが private のため `std::make_shared` できない
- `src/game/game_key_core.h` の `restore_termios`: カスタムデリーター付きの `std::shared_ptr<void>(new int(), ...)` のため `std::make_shared` できない

### boost::optional

- `src/zakuro.h` の `#include <boost/optional.hpp>` が残っているが、使用箇所は無い (`std::optional` に移行済み。CHANGES.md 2025.1.0 にも移行の記述がある)。
- この include の削除は「未使用シンボル・関数・include の削除」の issue が、`src/zakuro.cpp` の `// boost::optional<...>` コメント 5 箇所の削除は「コメントアウトされた古いコード・デバッグ痕跡の削除」の issue が担当するため、本 issue では扱わない。

## 設計方針

- `src/scenario_player.h` の `boost::variant` を `std::variant` に置換する
  - `boost::get<T>(opv)` → `std::get<T>(opv)`
  - `opv.which()` → `opv.index()`。`ScenarioData::Type` の列挙順と variant の型順が一致しているため、`switch (static_cast<ScenarioData::Type>(opv.index()))` で現状の分岐構造を保てる (または `std::visit` + visitor に書き換える)
  - `#include <boost/variant.hpp>` → `#include <variant>`
- `std::unique_ptr<T>(new T(...))` と `std::unique_ptr` メンバーへの `.reset(new T(...))` → `std::make_unique<T>(...)`
- `std::shared_ptr<T>(new T(...))` と `std::shared_ptr` メンバーへの `.reset(new T(...))` → `std::make_shared<T>(...)`
- `src/binary_pool.h` の配列 `new` → `std::make_unique<uint8_t[]>(size_)`
- 例外 (上記「置換しない」) はそのまま残す

## 完了条件

- `git grep -nE 'boost::variant' src/` で該当が 0 件になること
- `git grep -nE '\.reset\(new |std::unique_ptr<[^>]+>\(new |std::shared_ptr<[^>]+>\(new ' src/` で該当が 0 件になること (例外: `src/virtual_client.cpp` の `VirtualClient::Create` と `src/game/game_key_core.h` の `restore_termios` を除く)
- `src/main.cpp` の `log_sink` のように改行をまたぐ `new` は grep では検出できないため、上記 現状 の全箇所について置換・例外判断が完了していること
- ビルドとテストが通ること
- `boost::optional` の残存は本 issue の完了条件に含めない (対応する include ・コメントは他 issue が担当する)
