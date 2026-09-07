# WavReader の複数バグ (csize 符号拡張・16bit PCM 暗黙変換・テキストモード open)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-wav-reader-defects
- Polished: 2026-09-08

## 目的

`WavReader` に以下 3 種の問題がある。まとめて修正する。

- チャンクサイズの 4 バイト値を signed int で合成しており、MSB が立つ chunk サイズを正しく検出できず、特定の巨大な chunk サイズでクラッシュする
- 16bit signed PCM を unsigned オペランドの合成式として int16_t に暗黙変換しており、C++20 では値は正しく定まるが signed 16bit の意図が伝わらない
- ファイルを text mode で open しており、Windows で CRLF 変換によりバイナリが壊れる (将来 Windows 対応時)

## 現状

### csize 符号拡張

`src/wav_reader.cpp` の `ReadChunk` は以下のように書かれている。

```cpp
int csize = (int)buf[4] | ((int)buf[5] << 8) | ((int)buf[6] << 16) |
            ((int)buf[7] << 24);
if (size < csize + 8) return false;
```

`csize` は signed int。`buf[7] >= 0x80` で MSB が立ち、C++20 (CMakeLists.txt の
`CXX_STANDARD 20`) では 2 の補数として負値になる。

このとき `if (size < csize + 8)` は右辺を size_t へ暗黙変換して比較するため、`csize + 8` が
負なら常に true となりエラー返却される。素通りする (false になる) のは `csize + 8` が 0〜7 の
場合だけであり、chunk サイズの 4 バイト値が `0xFFFFFFF8` 〜 `0xFFFFFFFF` のときに限られる。
つまり 2GB 超の chunk サイズの大半は現在もエラー返却になり、実際に問題になるのは末尾 8 値のみである。

素通りした場合、`chunk_size = csize;` (size_t) で巨大値になり、`cbuf += 8 + chunk_size;` では
`8 + chunk_size` が size_t のラップで 0〜7 に化ける。data チャンクなら
`int n = chunk_size / 2;` が負値になり、`data.reserve(n)` が `std::length_error` を投げて
未捕捉例外となりクラッシュする。data 以外のチャンクなら進みが 0〜7 バイトのずれになり、
`0xFFFFFFF8` のときは進みが 0 で無限ループになる。

### 16bit PCM 暗黙変換

`src/wav_reader.cpp` の data チャンク読み込みは以下。

```cpp
data.push_back((int)p[0] | ((int)p[1] << 8));
```

合成式の型は int で、値域は 0x0000〜0xffff。`p[1] >= 0x80` のとき 0x8000〜0xffff になり、
`std::vector<int16_t>` の `data` への push_back で暗黙変換が発生する。C++20 ではこの変換は
2 の補数として定まり (例: `int16_t(0xffff) == -1`)、現行実装でも値は正しい。
ただし signed 16bit として読み出している意図が unsigned の合成式に隠れており、読解時のバグ源となる。

### テキストモード open

`src/wav_reader.cpp` の `Load(std::string path)` は `std::ifstream fin(path);` で開いており、
デフォルトが text mode。macOS / Linux では実質同じだが、Windows では CRLF 変換で WAV バイナリが壊れる。

## 設計方針

### csize 符号拡張

`ReadChunk` で `uint32_t csize` に変更し、`if (size < (size_t)csize + 8)` のように
size_t へ拡張してから比較する。負値化と 32bit 加算のラップを排除する。
(`csize + 8` のままでは uint32_t 同士の加算が 2^32 でラップし、`0xFFFFFFFF` で 7 に化けて
素通りするため、先に size_t へ変換する)
巨大値 (実ファイルサイズを超える chunk サイズ。上記の `0xFFFFFFF8` 〜 `0xFFFFFFFF` を含む) の
入力も明示的にエラー返却する。

### 16bit PCM 符号拡張

`int16_t s = static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));`
の形で明示的に符号付き変換する。読解時に「signed 16bit として扱う」意図が明確になる。

### テキストモード open

`std::ifstream fin(path, std::ios::binary);` に変更する。動作環境が macOS / Ubuntu のみでも
将来の Windows サポートに備え、意図を明示する意味で修正しておく。

## 完了条件

- chunk サイズ `0xFFFFFFFF` の data チャンクを含む WAV を `--fake-audio-capture` に指定して
  起動してもクラッシュせず、`failed to load fake audio` のエラーログが出力されること
  (AddressSanitizer 有効ビルドで確認)
- data チャンク読み込みの変換式が明示的な符号付き変換になっており、unsigned 合成値の
  暗黙変換がコードに残っていないこと
- `WavReader::Load(std::string path)` の `std::ifstream` が `std::ios::binary` で open されていること

なお、C++ 単体テスト基盤は未整備のため (issue 0043 で整備予定)、負サンプル値の単体テストは
単体テスト基盤の導入後に追加する。上記の完了条件は現状のテスト基盤 (pytest の E2E) で検証できる
範囲に限定している。
