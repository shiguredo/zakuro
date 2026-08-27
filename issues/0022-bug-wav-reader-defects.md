# WavReader の複数バグ (csize 符号拡張・16bit PCM 符号拡張欠落・テキストモード open)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-wav-reader-defects
- Polished: {YYYY-MM-DD}

## 目的

`WavReader` に以下 3 種の欠陥がある。まとめて修正する。

- チャンクサイズを signed int で合成しており、`buf[7] >= 0x80` で負値化 → size_t への暗黙変換で巨大値 → 以降 UB
- 16bit signed PCM を unsigned として合成し int16_t に暗黙変換しており、`p[1] >= 0x80` のサンプルの扱いが実装依存
- ファイルを text mode で open しており、Windows で CRLF 変換によりバイナリが壊れる (将来 Windows 対応時)

## 現状

### csize 符号拡張

`src/wav_reader.cpp` の `ReadChunk` は以下のように書かれている。

```cpp
int csize = (int)buf[4] | ((int)buf[5] << 8) | ((int)buf[6] << 16) |
            ((int)buf[7] << 24);
if (size < csize + 8) return false;
```

`csize` は signed int。`buf[7] >= 0x80` で MSB が立ち、C++20 では 2 の補数として負値になる。
`size_t chunk_size = csize;` で size_t への暗黙変換により巨大な正数になり、
`if (size < csize + 8)` の比較を素通りする。以降 `cbuf += 8 + chunk_size;` で UB。

### 16bit PCM 符号拡張欠落

`src/wav_reader.cpp` の data チャンク読み込みは以下。

```cpp
data.push_back((int)p[0] | ((int)p[1] << 8));
```

`(int)p[1] << 8` の結果は最大 `0xff00` (unsigned)。`p[1] >= 0x80` のとき、
値が 0x8000-0xffff の範囲になり、int16_t への push_back で暗黙変換が発生する。
C++20 以降は 2 の補数保証で `int16_t(0xffff) == -1` と定まるが、意図が unsigned で処理されている点で読解時のバグ源。

### テキストモード open

`src/wav_reader.cpp` の `Load(std::string path)` は `std::ifstream fin(path);` で開いており、
デフォルトが text mode。macOS / Linux では実質同じだが、Windows では CRLF 変換で WAV バイナリが壊れる。

## 設計方針

### csize 符号拡張

`ReadChunk` で `uint32_t csize` に変更し、`size_t` で比較する。負値化を排除する。
巨大値 (例: 2GB 超) の入力も明示的にエラー返却する。

### 16bit PCM 符号拡張

`int16_t s = static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));`
の形で明示的に符号付き変換する。読解時に「signed 16bit として扱う」意図が明確になる。

### テキストモード open

`std::ifstream fin(path, std::ios::binary);` に変更する。動作環境が macOS / Ubuntu のみでも
将来の Windows サポートに備え、意図を明示する意味で修正しておく。

## 完了条件

- 巨大 chunk_size を含む WAV を渡してもクラッシュせず、エラー返却で終わること
- 16bit signed PCM の負サンプルが正しく `int16_t` として扱われていること (単体テストで検証)
- 全ての `std::ifstream` の open が binary mode になっていること
