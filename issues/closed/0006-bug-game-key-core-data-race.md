# GameKeyCore::keys_ のデータレースと iterator invalidation

- Created: 2026-08-27
- Completed: 2026-09-28
- Branch: feature/fix-game-key-core-data-race
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`GameKeyCore` の `keys_` (std::vector<GameKeyInterface*>) が mutex 保護なしで
複数スレッドから同時に読み書きされており、iterator invalidation とデータレースが発生する経路を修正する。

## 現状

`src/game/game_key_core.h` の `GameKeyCore` は以下のように `keys_` を保護せずに扱っている。

- バックグラウンドスレッド (Init 内で起動) が読み取った入力を private な `PushKey(uint8_t)` に渡し、
  内部で `for (auto key : keys_) key->PushKey(c);` を実行してイテレートする
- `Register(GameKeyInterface*)` / `Unregister(GameKeyInterface*)` が `keys_.push_back` / `keys_.erase` を呼ぶ

`GameKey` (`src/game/game_key.h`) は `FakeAudioKeyTrigger` (`src/fake_audio_key_trigger.h`) のメンバとして生成・破棄される。
`FakeAudioKeyTrigger` は `Zakuro::Run` (`src/zakuro.cpp`) 内で生成され、`Zakuro::Run` は `main.cpp` の
`--config` の instances ごとに起動されるワーカースレッド上で実行されるため、`GameKey` の生成・破棄も
そのワーカースレッド上で行われる。
`GameKeyCore` は `main.cpp` の `key_core` (shared_ptr) として複数の Zakuro インスタンス間で共有される。
複数インスタンスが並行に `Register` / `Unregister` を呼び出す状況で、
バックグラウンドスレッドが `for (auto key : keys_) key->PushKey(c);` の最中に `keys_.erase` が走ると
iterator invalidation を起こしてダングリングポインタを dereference する。

## 設計方針

`keys_` の全アクセス経路に `std::mutex` を導入して排他制御する。

- `#include <mutex>` を追加し、メンバに `std::mutex keys_mutex_;` を追加
- `Register` / `Unregister` / private `PushKey` の全てで `std::lock_guard<std::mutex>` を取る
- `PushKey` のイテレート中に `Unregister` を待たせて良いので、シンプルに全体ロックで十分
  (キー入力頻度は最大 10 Hz 程度なので lock contention は問題にならない)

より軽量な代替として、`keys_` を `std::vector<std::shared_ptr<GameKeyInterface>>` に変更して
`weak_ptr` で扱う方法もあるが、`GameKeyInterface` を継承する `GameKey` の寿命が `FakeAudioKeyTrigger` に
束縛されている現状の構造を大きく変える必要があるため、mutex 追加が最小変更。

## 完了条件

- `Register` / `Unregister` / `PushKey` の全ての `keys_` アクセスが mutex で保護されていること
- 可能であれば ThreadSanitizer 有効ビルドで race が検知されないこと
- macOS / Linux で `--config` の `instances` に 2 件以上を定義して複数 Zakuro インスタンスを起動し、
  動作中にキー入力を投げても iterator invalidation による SIGSEGV / 異常挙動が発生しないこと

## 解決方法

2026-09-28 追記: `GameKeyCore` の `keys_` への全アクセスを `std::mutex keys_mutex_` で保護し、
キー入力の配送中に `GameKey` が破棄されても壊れないようにした。

### 変更内容

- `src/game/game_key_core.h` に `#include <mutex>` と `std::mutex keys_mutex_` を追加し、
  `Register` / `Unregister` / private `PushKey` の 3 経路すべてで `std::lock_guard` を取るようにした
- `PushKey` はロックを保持したまま `GameKeyInterface::PushKey` を呼ぶため、その実装から
  `Register` / `Unregister` へ再入してはならない契約を `GameKeyInterface` と `GameKeyCore::PushKey` の
  コメントに明記した
- `keys_mutex_` は `keys_` より先に宣言し、破棄は逆順で `keys_` の後になるようにした

### 追加したテスト

- `test/game_key_core_test.cpp` を追加し、`CMakeLists.txt` に `zakuro_game_key_core_test` を追加した
  - 8 スレッドから `GameKey` の生成と破棄を 500 回ずつ繰り返し、`Register` / `Unregister` の
    並行実行で `keys_` が壊れないことを確認する
  - pty を用意して標準入力を差し替え、キー入力を配送し続けている間に `GameKey` の生成と破棄を
    20000 回繰り返し、走査中の `Unregister` で壊れないことを確認する
  - テストフレームワークが未導入のため、実行ファイルの終了コードで合否を返す
    (`zakuro_adm_test` と同じ流儀)

### 検証結果

- `python3 run.py build macos_arm64` が成功した
- `ctest` が 2/2 成功した (`zakuro_adm_test` / `zakuro_game_key_core_test`)
- `uv run pytest` が 20 passed / 1 skipped だった (skip は Sora のシグナリング先が要る `test_version`)
- ThreadSanitizer 有効ビルドで race を検知しないこと。修正前のヘッダでは race を検知して
  異常終了し、修正後は 0 件で終了する (プロジェクト同梱の clang には TSan ランタイムが無いため、
  Apple clang で検証した)
- 修正前のヘッダに戻すと追加テストが 30 回中 30 回異常終了し、修正後は 30 回中 0 回だった。
  通常ビルドでも回帰を検出できる
- `clang-format --dry-run --Werror` の違反が無いこと

### 完了条件 3 について

「macOS / Linux で `--config` の `instances` に 2 件以上を定義して複数 Zakuro インスタンスを起動し、
動作中にキー入力を投げても SIGSEGV / 異常挙動が発生しないこと」は、Sora のシグナリング先が
必要でこの環境では実行できず未検証である。代わりに `test/game_key_core_test.cpp` で
`GameKeyCore` / `GameKey` を直接使って同じ経路 (走査中の `Unregister` と、生成・破棄と並行した配送) を
検証している。

### 設計方針の前提の訂正

設計方針にある「キー入力頻度は最大 10 Hz 程度」は入力側の制限ではない。
`GameKeyCore::Init` の背景スレッドは `select` に 100 ミリ秒のタイムアウトを与えているが、
読み取り可能なら直ちに返るため、標準入力へ連続して書き込めばその速度で `PushKey` が走る。
10 Hz は `FakeAudioKeyTrigger` が 100 ミリ秒ごとに 1 件消費する側のレートである。
ロック保持時間が無視できる根拠は、現在の `GameKey::PushKey` がキューへの追加だけで終わる点に
置き換えた。
