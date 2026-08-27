# namespace std に to_string(std::string) を追加していて undefined behavior

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-namespace-std-to-string-overload-ub
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`src/util.cpp` で `namespace std` に対して `to_string(std::string)` を追加しており、
C++ 標準 [namespace.std]/2 の「ユーザーが名前空間 std へテンプレート特殊化を除く関数・型を追加することは undefined behavior」規定に違反している。
しかも参照 0 件で全く使われていないため、削除する。

## 現状

`src/util.cpp` に以下のオーバーロードが定義されている。

```cpp
namespace std {

std::string to_string(std::string str) {
  return str;
}

}  // namespace std
```

コードベース全体を grep しても本関数の呼び出し箇所は 0 件。
`Util::PrimitiveValueToString` 経由でも使われていない。

C++ 標準ライブラリの改定で同名関数が追加されるとオーバーロード解決が未規定となり、
ABI / 挙動の破綻を招く可能性がある。

## 設計方針

該当ブロックを完全に削除する。
必要になった場合は独自の名前空間に置くか、`Util::ToString(const std::string&)` のようなユーティリティを定義する。

## 完了条件

- `src/util.cpp` から `namespace std { ... }` が削除されていること
- `git grep -n 'namespace std' src/` で該当箇所が 0 件になること
- ビルドと既存テスト (`test/test_zakuro.py::test_version` 含む) が全て pass すること
