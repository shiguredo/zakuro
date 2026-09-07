# BinaryPool の type-punning による strict aliasing 違反

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-binary-pool-alignment
- Polished: 2026-09-07

## 目的

`BinaryPool` のコンストラクタが `new uint8_t[]` のバッファに対して
`*(uint64_t*)p = ...` のキャスト書き込み (type-punning) を行っており、
C++ の strict aliasing 規則 ([basic.lval]) に違反する undefined behavior になっている。
これを除去する。

## 現状

`src/binary_pool.h` の `BinaryPool` コンストラクタは以下を実行している。

```cpp
bin_.reset(new uint8_t[size_]);
uint8_t* p = bin_.get();
for (int i = 0; i < n; i++) {
  *(uint64_t*)p = engine_();
  p += sizeof(uint64_t);
}
```

`uint8_t*` を `uint64_t*` にキャストして書き込む操作は、`new uint8_t[]` で生成された
`uint8_t` オブジェクトを `uint64_t` の glvalue 経由でアクセスしており、[basic.lval] の
aliasing rule に違反するため undefined behavior である。

実害 (クラッシュなど) は確認できていない点に注意する。アラインメントについては、
デフォルトの `operator new` / `operator new[]` は C++17 以降
`__STDCPP_DEFAULT_NEW_ALIGNMENT__` (64 ビット環境では `alignof(max_align_t)` と同じ値、
g++ 13 / clang 18 の x86_64 で実測 16) に整列させたストレージを返すことが規格で保証されており、
`alignof(uint64_t)` (実測 8) は常に満たされる。したがってアラインメント違反は発生せず、
macOS arm64 (動作環境: README.md) で SIGBUS するという懸念は誤りである。
UBSan (`-fsanitize=undefined,alignment`) / ASan も type-punning を検知しない
(現行コードをそのままビルドしても UBSan の報告はないことを x86_64 で実測確認済み)。

検出できるのは静的解析のみ。Clang の `-Wcast-align` (GCC は `-Wcast-align=strict`) は
本コードに次の警告を出す (clang 18 で実測)。

```
cast from 'uint8_t *' (aka 'unsigned char *') to 'uint64_t *'
    (aka 'unsigned long *') increases required alignment from 1 to 8
```

## 設計方針

以下のいずれかで対処する。

- **std::memcpy 化**: `uint64_t value = engine_(); std::memcpy(p, &value, sizeof(uint64_t));` に置換
  - strict aliasing 違反を解消し、`-Wcast-align` の警告も消える。最も無難
  - `binary_pool.h` に `#include <cstring>` を追加する (現状は include していない)
- **std::vector<uint64_t> 化**: `bin_` を `std::vector<uint64_t>` に変え、
  `Get` の中でだけ `reinterpret_cast<const uint8_t*>(bin_.data())` として扱う
  - `std::vector<uint64_t>` は `uint64_t` の配列として確保されるためアラインメントも
    正しく、`Get` は char 型経由の読み出しなので strict aliasing 違反にならない
  - `Get` の `(const char*)bin_.get()` を `bin_.data()` 経由に変更する必要があり、
    変更範囲は memcpy 化より広い

前者の `std::memcpy` が変更範囲が最小。コンパイラは最適化で `mov` 1 命令に潰す。

## 完了条件

- `src/binary_pool.h` の `BinaryPool` コンストラクタから `*(uint64_t*)p` の type-punning が
  排除されていること (memcpy 化または `std::vector<uint64_t>` 化)
- Clang の `-Wcast-align` (または GCC の `-Wcast-align=strict`) 付きでビルドが通り、
  `binary_pool.h` に起因する警告が 0 件であること (現行コードでは 1 件警告が出る)
- `--sora-data-channels` 経由のシナリオでデータチャネルメッセージの送受信が
  従来どおり動作すること (生成されるバイナリは疑似乱数のため実質の挙動は変わらない)。
  アラインメント由来のクラッシュは元々発生しないため、実機での再現試験は不要。
  なお、UBSan / ASan を有効にするビルドオプションは現状リポジトリに存在しないため、
  検証は静的解析で行う
