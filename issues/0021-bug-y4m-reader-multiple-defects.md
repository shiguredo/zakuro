# Y4MReader の複数バグ (FILE* リーク・fps_den_ 0 除算・I420Buffer stride 前提)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-y4m-reader-multiple-defects
- Polished: {YYYY-MM-DD}

## 目的

`Y4MReader` に以下 3 種の欠陥がある。いずれも稀ながら実運用で踏みうるので、まとめて修正する。

- ファイルオープン後に file_size 取得失敗すると FILE* がリークする
- Y4M ヘッダの `F` フィールド分母が 0 のとき除算エラーになる
- `GetFrame` が Y+U+V を一括で `MutableDataY()` に書き込んでおり、`I420Buffer` の内部レイアウトに関する暗黙前提が壊れると U/V プレーンが壊れる

## 現状

### FILE* リーク

`src/y4m_reader.cpp` の `Y4MReader::Open` は `fopen` 成功後、
`boost::filesystem::file_size` の `error_code` を確認して失敗時 `return -2;` するが、
その時点でまだ `file_.reset(fp, ...)` を実行していない。`fp` が RAII に載らないままリークする。

### fps_den_ 0 除算

`Y4MReader::ReadHeader` は `if (width_ == 0 || height_ == 0 || fps_num_ == 0)` はチェックするが、
`fps_den_ == 0` はチェックしていない。不正な Y4M (`F30:0` など) を渡すと
`Y4MReader::GetFrame` の `int frame = ms.count() * fps_num_ / (1000 * fps_den_);` で 0 除算になる。

### I420Buffer stride 前提

`src/fake_video_capturer.cpp` の Y4M 分岐で、
`y4m_reader_.GetFrame(now, y4m_buffer_->MutableDataY(), &updated);` として
`GetSize() = width*height + (width+1)/2 * (height+1)/2 * 2` バイトを `MutableDataY()` に一括で書き込んでいる。

`webrtc::I420Buffer` は内部でプレーンごとに stride を持っており、`Y` の後 `width*height` の位置に必ずしも `U` プレーンの先頭が来る保証はない (16 バイト境界でパディングされる実装がある)。
現状の一括書き込みは、`I420Buffer` の実装が「Y/U/V 連続・stride==width」であることに依存している。

## 設計方針

### FILE* リーク

`fopen` 直後に `file_.reset(fp, [](FILE* fp) { ::fclose(fp); });` を実行し、
その後で `file_size` 取得を行う。エラー時は RAII で自動 close される。

### fps_den_ 0 除算

`ReadHeader` の validation を `if (width_ == 0 || height_ == 0 || fps_num_ == 0 || fps_den_ == 0)` に拡張する。

### I420Buffer stride 前提

以下いずれかで対処する。

- プレーンごとに `MutableDataY()` / `MutableDataU()` / `MutableDataV()` を取得し、行単位で `stride` を考慮して `memcpy` する
- Y4M の読み込み専用に width と等しい stride を確定させた `I420Buffer` の subclass を用意する

libwebrtc の `I420Buffer::Create` は現状 `stride == width` を返す実装が多いが、これは仕様保証ではないので
明示的に stride を扱う実装に修正する。

## 完了条件

- `Open` で `file_size` 取得失敗時に FILE* がリークしないこと (Valgrind で確認)
- `fps_den_ == 0` の Y4M を渡した際に除算エラーではなくエラー返却で終わること
- `I420Buffer` の内部 stride が変わっても Y4M 経路の映像が正しく生成されること
