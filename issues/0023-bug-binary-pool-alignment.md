# BinaryPool のアラインメント違反 (strict aliasing / alignment)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-binary-pool-alignment
- Polished: {YYYY-MM-DD}

## 目的

`BinaryPool` のコンストラクタで `new uint8_t[]` に対して `*(uint64_t*)p = ...` の
type-punning + 書き込みを行っており、アラインメントおよび strict aliasing 違反になっている。
macOS arm64 が動作環境に含まれているため、実際に SIGBUS を踏む可能性がある。

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

`new uint8_t[]` は `alignof(uint64_t)` の保証を出さない (実装依存で `max_align_t` に揃うことはあるが、規格上は不定)。
また `uint8_t*` を `uint64_t*` にキャストして書き込むのは strict aliasing 違反。

x86_64 では unaligned アクセスが許容されるためこれまで動いていたが、
ARM 系 (macOS arm64 が動作環境) で SIGBUS するリスク、および UBSan / ASan 検査での UB 検出リスクがある。

## 設計方針

以下のいずれかで対処する。

- **std::memcpy 化**: `uint64_t value = engine_(); std::memcpy(p, &value, sizeof(uint64_t));` に置換
  - strict aliasing / alignment いずれも解消。最も無難
- **std::vector<uint64_t> 化**: `bin_` を `std::vector<uint64_t>` に変え、
  `Get` の中でだけ `reinterpret_cast<const uint8_t*>(bin_.data())` として扱う
  - `std::vector<uint64_t>` はアラインメントが保証される

前者の `std::memcpy` が変更範囲が最小。コンパイラは最適化で `mov` 1 命令に潰す。

## 完了条件

- `BinaryPool` の初期化コードから type-punning が排除されていること
- macOS arm64 の実機、または UBSan / ASan 有効ビルドで `--sora-data-channels` 経由の負荷試験を実行し、
  alignment / strict aliasing 系の UB が検知されないこと
