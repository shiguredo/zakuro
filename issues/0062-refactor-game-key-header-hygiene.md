# GameKey のヘッダが自己完結しておらず未使用メンバーが残っている

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-game-key-header-hygiene
- Polished: 2026-09-30

## 目的

`src/game/game_key.h` の `GameKey` に `GameKeyCore` からコピーされたと思われる未使用メンバーが残っており、
`std::mutex` / `std::lock_guard` を使いながら `<mutex>` を直接インクルードしていない。
ヘッダを自己完結かつ最小にして、読解時の混乱と将来の変更時の事故を減らす。

## 現状

### 未使用メンバー

`src/game/game_key.h` の `GameKey` は次の 2 つのメンバーを持つが、どちらも参照されていない。

- `th_` (`std::unique_ptr<std::thread>`)
- `stopped_` (`std::atomic_bool`)

`GameKey` にはスレッドを開始する経路 (`Init` / `Reset`) が無く、スレッドを持っているのは
`FakeAudioKeyTrigger` と `GameKeyCore` である。`GameKey` 側はそのコピーの残骸と考えられる。
`GameKey` が実際に使うメンバーは `mutex_` / `queue_` / `core_` の 3 つである。

### 暗黙の include 依存

`GameKey::PushKey` と `GameKey::PopKey` は `std::mutex` と `std::lock_guard` を使うが、
`src/game/game_key.h` は `<mutex>` をインクルードしていない。
`game_key_core.h` が `<mutex>` をインクルードしているため推移的にコンパイルできているだけであり、
`game_key_core.h` 側から `<mutex>` が外れると `game_key.h` がコンパイルできなくなる。

## 設計方針

- `src/game/game_key.h` に `#include <mutex>` を追加する
- 未使用の `th_` と `stopped_` を削除する
- 削除に伴い不要になる include (`<atomic>` / `<thread>` / `<chrono>` / `<sys/select.h>` / `<termios.h>` / `<unistd.h>`)
  を洗い出して削除する。実際に必要かどうかはビルドで確認する
  (`std::chrono` と `select` は `game_key.h` 内で使用していない)
- 単体インクルードの完了条件を満たすには、推移的に含まれる `src/game/game_key_core.h` も自己完結である必要がある。
  `game_key_core.h` の `Unregister` は `std::remove_if` を `<algorithm>` をインクルードせずに使っており、
  他のインクルードに依存してコンパイルできているため、`#include <algorithm>` を追加する
- 挙動は変えないため、ビルドとテストで回帰が無いことを確認する

## 完了条件

- `src/game/game_key.h` に `<mutex>` のインクルードがあること
- `GameKey` に未使用メンバーが無いこと
- `src/game/game_key.h` を単体でインクルードしてもコンパイルできること
- `python3 run.py build macos_arm64` と `uv run pytest` が通ること
