# GameKeyCore::keys_ のデータレースと iterator invalidation

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-game-key-core-data-race
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`GameKeyCore` の `keys_` (std::vector<GameKeyInterface*>) が mutex 保護なしで
複数スレッドから同時に読み書きされており、iterator invalidation とデータレースが発生する経路を修正する。

## 現状

`src/game/game_key_core.h` の `GameKeyCore` は以下のように `keys_` を保護せずに扱っている。

- バックグラウンドスレッド (Init 内で起動) が読み取った入力を private な `PushKey(uint8_t)` に渡し、
  内部で `for (auto key : keys_) key->PushKey(c);` を実行してイテレートする
- `Register(GameKeyInterface*)` / `Unregister(GameKeyInterface*)` が `keys_.push_back` / `keys_.erase` を呼ぶ

`GameKey` (`src/game/game_key.h`) は `FakeAudioKeyTrigger` のメンバとしてメインスレッドで生成・破棄される。
`GameKeyCore` は `main.cpp` の `key_core` (shared_ptr) として複数の Zakuro インスタンス間で共有される。
複数インスタンスが並行に `Register` / `Unregister` を呼び出す状況で、
バックグラウンドスレッドが `for (auto key : keys_) key->PushKey(c);` の最中に `keys_.erase` が走ると
iterator invalidation を起こしてダングリングポインタを dereference する。

## 設計方針

`keys_` の全アクセス経路に `std::mutex` を導入して排他制御する。

- メンバに `std::mutex keys_mutex_;` を追加
- `Register` / `Unregister` / private `PushKey` の全てで `std::lock_guard<std::mutex>` を取る
- `PushKey` のイテレート中に `Unregister` を待たせて良いので、シンプルに全体ロックで十分
  (キー入力頻度は最大 10 Hz 程度なので lock contention は問題にならない)

より軽量な代替として、`keys_` を `std::vector<std::shared_ptr<GameKeyInterface>>` に変更して
`weak_ptr` で扱う方法もあるが、`GameKeyInterface` を継承する `GameKey` の寿命が `FakeAudioKeyTrigger` に
束縛されている現状の構造を大きく変える必要があるため、mutex 追加が最小変更。

## 完了条件

- `Register` / `Unregister` / `PushKey` の全ての `keys_` アクセスが mutex で保護されていること
- ThreadSanitizer 有効ビルドで race が検知されないこと
- 複数 Zakuro インスタンスを起動した状態でキー入力を投げ、iterator invalidation による SIGSEGV / 異常挙動が発生しないこと
