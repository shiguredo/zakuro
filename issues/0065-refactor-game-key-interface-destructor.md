# GameKeyInterface のデストラクタが非仮想で GameKeyInterface* 経由の削除が未定義動作になる

- Created: 2026-09-28
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-game-key-interface-destructor
- Polished: {YYYY-MM-DD}

## 目的

`GameKeyInterface` は `GameKeyCore::keys_` に `GameKeyInterface*` として保持される抽象クラスだが、
デストラクタが非仮想である。派生クラスのオブジェクトを `GameKeyInterface*` 経由で `delete` すると
未定義動作になるため、安全な形に整える。

## 現状

- `src/game/game_key_core.h` の `GameKeyInterface` は `~GameKeyInterface() {}` を持つが `virtual` が付いていない
- `GameKeyCore::keys_` は `std::vector<GameKeyInterface*>` で、`Register` / `Unregister` は生ポインタを受け取る
- 派生は `GameKey` のみで、`GameKey` は `FakeAudioKeyTrigger` のメンバとして値で生成・破棄される。
  そのため現時点で `GameKeyInterface*` 経由の `delete` は存在せず、実害は出ていない
- 非仮想デストラクタのため、`GameKey` を `std::unique_ptr` や `std::optional` のような
  値として破棄するコンテナで扱うと `-Wdelete-non-abstract-non-virtual-dtor` の警告が出る

## 設計方針

以下いずれかにする。

- `virtual ~GameKeyInterface() {}` にして、`GameKeyInterface*` 経由の削除を安全にする
- 仮想デストラクタにせず、`GameKeyCore` が所有しない (削除しない) ことをコメントで明示し、
  生ポインタを渡す API の契約をコードに残す

`PushKey` は純粋仮想のまま変更しない。`GameKeyCore` の `keys_` を所有権付きの型に変えるかどうかは
本 issue では扱わない。

## 完了条件

- `GameKeyInterface*` 経由の削除が安全であるか、削除しない設計であることがコードとコメントで明確であること
- `-Wdelete-non-abstract-non-virtual-dtor` を有効にしても `GameKey` の破棄で警告が出ないこと
- `python3 run.py build macos_arm64` と `ctest` が通ること
