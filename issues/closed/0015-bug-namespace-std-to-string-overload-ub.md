# namespace std に to_string(std::string) を追加していて undefined behavior

- Created: 2026-08-27
- Completed: 2026-09-29
- Branch: feature/fix-namespace-std-to-string-overload-ub
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`src/util.cpp` で `namespace std` に対して `to_string(std::string)` を追加しており、
C++ 標準 [namespace.std]/1 の「明示的に許される場合（プログラム定義型に依存するテンプレート特殊化など）を除き、名前空間 std または std 内の名前空間に宣言や定義を追加するプログラムの挙動は undefined behavior」規定に違反している。
CLI11 の既定値表示の経路から ADL 経由で実際に選択されていたため、削除する。

## 現状

`src/util.cpp` に以下のオーバーロードが定義されている。

```cpp
namespace std {

std::string to_string(std::string str) {
  return str;
}

}  // namespace std
```

`src/` を grep しても本関数の直接の呼び出し箇所は 0 件である。ただし **ADL (実引数依存の名前探索) 経由で
CLI11 から選択されていた**。

- CLI11 の `CLI::detail::checked_to_string` は `to_string(std::forward<T>(value))` と非修飾で呼ぶ
- `CLI::App::add_option` は `std::string&` の変数を受け取るオプションで
  `checked_to_string<std::string, std::string>` を instantiate する
- 引数の型が `std::string` なので関連名前空間は `std` であり、`namespace std` に追加した
  このオーバーロードが選択されていた
- 削除後は CLI11 自身のオーバーロード (`std::string&` を返すもの) に解決され、
  返る値は変わらない (値のコピーが 1 回減る)

したがって本 issue は「未使用だから消す」ではなく「名前空間 `std` への追加という UB を、
CLI11 の既定値表示経路が実際に踏んでいたので消す」という位置づけになる。

## 設計方針

該当ブロックを完全に削除する。
必要になった場合は独自の名前空間に置くか、`Util::ToString(const std::string&)` のようなユーティリティを定義する。

## 完了条件

- `src/util.cpp` から `namespace std { ... }` が削除されていること
- `git grep -n 'namespace std' src/` で該当箇所が 0 件になること
- ビルドと既存テスト (`test/test_zakuro.py::test_version` 含む) が全て pass すること

## 解決方法

`src/util.cpp` から `namespace std` に追加していた `to_string(std::string)` のブロックを削除した。

- `src/` に `namespace std` の追加は 0 件になり、`to_string` の呼び出しはすべて
  `std::to_string(<算術型>)` になった
- 削除後もビルドが通り、`nm` で削除したシンボルがバイナリに残っていないことを確認した
- 削除前に ADL 経由で選択されていた CLI11 の経路は、削除後は CLI11 自身のオーバーロードに
  解決される。`--help` の出力を削除前後で比較し、完全に一致することを確認した
- `test/test_config_json.py` などの既存テストが pass することを確認した
  (`uv run pytest -q` が 90 passed / 1 skipped。skip は実 Sora 接続用の環境変数が
  無い環境での `test_zakuro.py::test_version` であり、CI では secrets が渡されて実行される)
- `clang-format -style=file` が `src/` の全ファイルで差分を出さない

なお issue 0038 の「未使用 include」に、レビューで見つかった `src/util.cpp` の
`<boost/beast/version.hpp>` と `<boost/preprocessor/stringize.hpp>` を追記した
(本 issue の変更で未使用になったものではないため、削除は 0038 で行う)。
