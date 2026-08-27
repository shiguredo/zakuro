# Boost.Variant / Boost.Optional の std 化と make_unique 置換

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-boost-legacy-and-make-unique
- Polished: {YYYY-MM-DD}

## 目的

C++20 で書かれているコードに、C++11/14 時代の `boost::variant` / `boost::optional` / 直接 `new T` / `unique_ptr(new T)` などが残っている。
標準ライブラリに寄せてモダン化する。

## 現状

### boost::variant

`src/scenario_player.h` で `boost::variant<...>` + `boost::get<T>(opv)` を使用。
C++17 以降 `std::variant` + `std::visit` があり、Boost 依存を減らせる。

### boost::optional

`src/zakuro.h` に `#include <boost/optional.hpp>` があるが使用箇所無し (別 issue で削除)。
CHANGES.md 2025.1.0 で `boost::optional` → `std::optional` に置換済みのはずが include が残っている。

### 直接 new T / unique_ptr(new T)

以下は `std::make_unique` に置換可能。

- `src/main.cpp`: `log_sink(new webrtc::FileRotatingLogSink(...))`, `stats_th.reset(new std::thread(...))`, `ths.push_back(std::unique_ptr<std::thread>(new std::thread(...)))`
- `src/http_server.cpp`: `thread_.reset(new std::thread([this] { Run(); }))`, `acceptor_.reset(new boost::asio::ip::tcp::acceptor(ioc_, endpoint))`
- `src/virtual_client.cpp`: `std::shared_ptr<VirtualClient>(new VirtualClient(config))` (private constructor のため make_shared 不可、ここは残す)
- `src/fake_video_capturer.cpp`: `thread_.reset(new std::thread(...))`
- `src/fake_audio_key_trigger.h`: `th_.reset(new std::thread([this] { ... }))`
- `src/zakuro.cpp`: `gam.reset(new GameAudioManager())`, `vc_config.fake_audio.reset(new FakeAudioData())`, `spc.binary_pool.reset(new BinaryPool(BINARY_POOL_SIZE))`, `trigger.reset(new FakeAudioKeyTrigger(...))`
- `src/zakuro_audio_device_module.cpp`: `fake_audio_.reset(new FakeAudioData())`, `audio_thread_.reset(new std::thread(...))`

## 設計方針

- `boost::variant` → `std::variant`, `boost::get<T>(v)` → `std::get<T>(v)` or `std::visit`
- 未使用の `boost::optional` include を削除 (別 issue と統合可能)
- `.reset(new T(...))` / `std::unique_ptr<T>(new T(...))` を `std::make_unique<T>(...)` に置換
  - private constructor 経由の shared_ptr は例外として残す

## 完了条件

- `git grep -n 'boost::variant\|boost::optional' src/` で該当が 0 件になること
- `git grep -nE '\.reset\(new |std::unique_ptr<[^>]+>\(new ' src/` で該当が 0 件 (または例外のみ) になること
- ビルドとテストが通ること
