# ParseDataChannels の interval バリデーションに到達不能コードがある

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-parse-data-channels-unreachable-code
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` から呼ばれる `ParseDataChannels` の `interval` バリデーション分岐で
`return false;` の後に `obj.erase(it);` が置かれ、永久に実行されないコードが残っている問題を修正する。
他の `size-min` / `size-max` などのバリデーションでは正常に `obj.erase(it);` が呼ばれており、
`interval` だけ整合性が欠落している。

## 現状

`src/zakuro.cpp` の `ParseDataChannels` の `interval` バリデーションブロックは以下のように書かれている。

```cpp
if (ch.interval <= 0) {
  std::cout << __LINE__ << std::endl;
  return false;
  obj.erase(it);
}
```

`return false;` の後に `obj.erase(it);` があり、この行は永久に実行されない。
他の項目 (`size-min`, `size-max`, `ordered` など) では `obj.erase(it);` が正しく呼ばれているのに対して、
`interval` だけ挙動が違う。

コンパイラの警告レベル次第で dead code 警告が出る。

## 設計方針

`interval` の挙動を他項目に揃える。以下のいずれか（実装者の判断）。

- 意図が「不正 interval で bail out」なら `obj.erase(it);` を削除し `return false;` だけ残す
- 意図が他項目と同様に「erase して継続」なら `return false;` を削除し `obj.erase(it);` の後に `continue` 相当の処理にする

現状の `size-min` / `size-max` は「不正値なら return false」する仕様で、`interval` もそれに揃えるのが自然。
このため前者（`obj.erase(it);` を削除）を推奨する。

## 完了条件

- `ParseDataChannels` に到達不能コードが 0 件になること
- `interval` バリデーションの挙動が仕様として説明可能なこと (`return false;` のみで一貫)
- `-Wunreachable-code` 相当を有効にしてもコンパイル警告が出ないこと
