# WavReader::Load の領域確保に失敗すると未捕捉例外で強制終了する

- Created: 2026-09-30
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-wav-reader-uncaught-exception
- Polished: 2026-09-30

## 目的

`WavReader::Load(std::string path)` がサンプル用の領域を確保できなかった場合、
`std::bad_alloc` が `Zakuro::Run` まで伝播して未捕捉例外となり、`std::terminate` を
経て SIGABRT でプロセスが強制終了する。確保に失敗しても読み込み失敗として扱い、
設定ミスや異常な入力と同じくエラー終了できるようにする。

## 現状

`src/wav_reader.cpp` の `WavReader::Load(std::string path)` は、ファイル全体を
`std::string` に読み込んでから `Load(const void*, size_t)` の戻り値をそのまま返す。
例外処理は無い。

```cpp
  return Load(buf.c_str(), buf.size());
```

`Load(const void*, size_t)` は data チャンクのサンプルを読む前に
`size_t n = chunk_size / 2;` を計算して `data.reserve(n)` を呼ぶ。チャンクサイズの
検査は `ReadChunk` の `if (size < (size_t)csize + 8)` で行われ、`chunk_size` は
残りの実ファイルサイズ以下に制限される。そのため `n` は実ファイルサイズの半分以下に
収まるが、`data` は `n` 個の `int16_t` を保持するためそのバイト数はチャンクサイズ
(`n * 2` バイト) と同程度となり、読み込んだ `buf` と合わせて
**実ファイルサイズの 2 倍程度のメモリが必要**になる。

確保に失敗すると `std::bad_alloc` が投げられる。`std::vector::reserve` は
`n > max_size()` の場合に `std::length_error` を投げるが、`n` は size_t で
`chunk_size / 2` であり `chunk_size` が 32bit に収まるため、こちらは発生しない。

`Zakuro::Run` には try / catch が無いため、`src/zakuro.cpp` の
`wav_reader.Load(config_.fake_audio_capture)` から例外が伝播すると
未捕捉例外で強制終了する。`src/voice_number_reader.h` の `Concat` も
`Load(const void*, size_t)` を例外処理なしで呼んでいる。

ファイル全体の読み込み (`ss << fin.rdbuf()` / `buf = ss.str()`) でも同じ大きさの
領域を確保するため、メモリが足りない場合はそちらでも `std::bad_alloc` が発生しうる。

## 設計方針

- 対象はユーザーが指定する WAV ファイル (`--fake-audio-capture`) の読み込み経路
  (`WavReader::Load(std::string path)`) のみとする。`src/voice_number_reader.h` の
  `Concat` が呼ぶ `Load(const void*, size_t)` は、ビルド時に固定される数十 KB の
  埋め込み音声番号 WAV が対象で、`Concat` にはエラーの受け渡し経路が無い
  (debug ビルドの `assert` のみ) ため、今回の対処からは外す
- `src/wav_reader.cpp` の `WavReader::Load(std::string path)` で `Load(ptr, size)` を
  try / catch で包み、`std::bad_alloc` などの例外を捕捉して読み込み失敗として扱う
- 捕捉した場合は英語のログを出してから非ゼロを返す。戻り値は 0 以外なら呼び出し側が
  エラーとして扱うため、既存の値と衝突しない値を割り当てる
- ファイル全体の読み込みでも例外が出るため、try の範囲は読み込みを含めるか、
  読み込み部分の失敗も同じ戻り値で扱えるようにする
- `src/wav_reader.h` に戻り値の一覧をコメントとして追加する。現状は戻り値の意味が
  コードを読まないと分からず、`-1` が「データが短すぎる」以外の意味でも使われると
  原因を切り分けられない
- モックやスタブは使わず、実バイナリを起動する E2E テストで検証する。ただし
  実際にメモリを枯渇させる WAV を用意するのは現実的でないため、検証方法は実装時に決めて
  明記する (確保の失敗を注入する単体テストは、C++ のテスト基盤が無いため使えない)

## 完了条件

- `WavReader::Load(std::string path)` が領域の確保に失敗した場合に、未捕捉例外で
  強制終了せず非ゼロの戻り値を返すこと
- 確保に失敗した場合の戻り値が、既存の戻り値の意味と衝突しないこと
- `src/wav_reader.h` に戻り値の一覧がコメントされていること
- `python3 run.py build macos_arm64` が通ること
- 既存の pytest が全て通ること

## 解決方法

実装時に記入する。
