# WavReader の複数バグ (csize 符号拡張・16bit PCM 暗黙変換・テキストモード open)

- Created: 2026-08-27
- Completed: 2026-09-30
- Branch: feature/fix-wav-reader-defects
- Polished: 2026-09-08
- Updated: 2026-09-29
- Milestone: 2026.1.0

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
  起動してもシグナルで異常終了せず、`failed to load fake audio` が標準エラー出力に出ること
  (`main` が `Zakuro::Run` の返り値を捨てているため終了コードでは判定できない。issues/0031 で解消予定)
- data チャンク読み込みの変換式が明示的な符号付き変換になっており、unsigned 合成値の
  暗黙変換がコードに残っていないこと
- `WavReader::Load(std::string path)` の `std::ifstream` が `std::ios::binary` で open されていること

なお、C++ 単体テスト基盤は issues/0066 で CTest とともに撤去済みで、テストは実バイナリを起動する pytest に
一本化されている (issue 0043 も整備予定を取り下げ済み)。負サンプル値の変換は pytest から直接観測できないため、
検証方法 (コードレビューで担保する / 対象外とする) を実装時に決めて明記する。
WAV の不正データに対する E2E の回帰テストは issues/0043 の項目 5 が扱う。
AddressSanitizer 有効ビルドの手段はリポジトリに無く、issues/0036 で追加が提案されているため、
サニタイザでの確認は本 issue の完了条件に含めない。

## 解決方法

3 項目を修正した。

### csize の符号拡張

`src/wav_reader.cpp` の `ReadChunk` でチャンクサイズを `uint32_t` として合成し、
比較を `if (size < (size_t)csize + 8)` に変更した。`uint32_t` のまま加算すると
2^32 でラップして `0xFFFFFFFF` が 7 に化けるため、先に `size_t` へ拡張してから比較する。

修正前は signed で合成していたため `csize + 8` が 0〜7 になる `0xFFFFFFF8`〜`0xFFFFFFFF` で
比較が偽になり、実ファイルサイズを超えるチャンクサイズを素通りさせていた。
素通りすると `chunk_size` が `SIZE_MAX` 付近の巨大値になり、`int n = chunk_size / 2` が
負値となって `data.reserve` が `std::length_error` を投げ、`Zakuro::Run` に
try / catch が無いため未捕捉例外でプロセスが強制終了していた
(実バイナリで終了コード 134 = SIGABRT を再現)。

あわせて `WavReader::Load(std::string path)` で `Load(ptr, size)` を try / catch で包み、
`std::bad_alloc` などの例外を読み込み失敗として扱うようにした。実ファイルサイズと整合する
巨大なチャンクサイズを受理した場合に、サンプル用の領域の確保が失敗しうるため。

### 16bit PCM の明示的な符号付き変換

data チャンクのサンプルを次の形で読むようにした。

```cpp
int16_t s = static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
```

修正前は unsigned の合成式からの暗黙変換で、値は C++20 では正しく定まるものの
signed 16bit として扱う意図が読み取れなかった。

### テキストモード open

`std::ifstream` を `std::ios::binary` 付きで開くようにした。macOS / Linux では
挙動は変わらないが、Windows で CRLF 変換により WAV バイナリが壊れるのを防ぐ。

### テスト

`test/test_main_resource.py` に次を追加した。

- `test_invalid_wav_data_chunk_size_exits_without_crash`: data チャンクサイズを
  `0xFFFFFFFF` / `0xFFFFFFF8` / `0x80000000` / `0x7FFFFFFF` にした 4 ケースをパラメータ化し、
  シグナルで異常終了せず終了コード 1 で `failed to load fake audio` を出力することを検証する
- `test_valid_wav_is_accepted`: 負値を含む 16bit PCM の WAV が受理されることを検証する。
  同じ設定で不正な WAV を渡すと必ず読み込み失敗になる対照ケースを先に実行し、
  読み込み経路を通っていることを確認する

`test_run_failure_exits_with_nonzero` と共通のセットアップ・アサーションは
`_fake_audio_config_path` / `_assert_fake_audio_load_failure` に集約した。

### 検証方法

- 修正前後のバイナリを実測し、`0xFFFFFFFF` / `0xFFFFFFF8` が修正前は SIGABRT (134)、
  修正後は終了コード 1 で `result=-10` になることを確認した
- `0x80000000` / `0x7FFFFFFF` は修正前から拒否されており、拒否の経路が壊れていないことを
  確認する境界値として残した
- 16bit PCM の負サンプル変換は pytest から直接観測できない (unsigned の合成式に戻しても
  2 の補数として同じ値になる) ため、**コード上の明示的な `static_cast` で担保し、
  テストの docstring に検出できない旨を明記した**
- `python3 run.py build macos_arm64` が通り、`test/` 配下の pytest が
  81 passed / 1 skipped (skip は実 Sora 接続用の環境変数が無い既存テスト) になることを確認した
- prek のフック (ruff format / ruff check / ty / 組み込みフック) が全て通ることを確認した

### 完了条件の確認

- chunk サイズ `0xFFFFFFFF` の data チャンクを含む WAV で異常終了せず
  `failed to load fake audio` が出ること: パラメータ化した E2E テストで検証した
  (終了コード 1 で判定する。`main` が `Zakuro::Run` の戻り値を終了コードに反映する)
- data チャンク読み込みの変換式が明示的な符号付き変換になっており、unsigned 合成値の
  暗黙変換がコードに残っていないこと: `src/wav_reader.cpp` の `static_cast<int16_t>` で担保した
- `WavReader::Load(std::string path)` の `std::ifstream` が `std::ios::binary` で
  open されていること: コードで担保した

なお、WAV の不正データに対する E2E の回帰テストは issues/0043 の項目 5 が扱うとしていたが、
本 issue の完了条件を満たすため data チャンクサイズ 4 値のテストを本 issue で追加した。
0043 の項目 5 は残る範囲 (fmt チャンクの短小・0 バイト・fmt の値不正・Y4M) を扱う。
