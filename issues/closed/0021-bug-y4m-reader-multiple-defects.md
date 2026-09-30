# Y4MReader の複数バグ (FILE* リーク・fps_den_ 0 除算・I420Buffer stride 前提)

- Created: 2026-08-27
- Completed: 2026-09-30
- Branch: feature/fix-y4m-and-wav-reader-defects
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

Y4M 経路 (`Y4MReader` の 2 件と `FakeVideoCapturer` の 1 件) に以下 3 種の欠陥がある。いずれも稀ながら実運用で踏みうるので、まとめて修正する。

- ファイルオープン後に file_size 取得失敗すると FILE* がリークする
- Y4M ヘッダの `F` フィールド分母が 0 のとき除算エラーになる
- Y4M フレームを `GetFrame` の書き込み先として渡した `MutableDataY()` に一括で書き込んでおり、`I420Buffer` の内部レイアウトに関する暗黙前提が壊れると U/V プレーンが壊れる

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

`webrtc::I420Buffer` は内部でプレーンごとに stride を持っており、`I420Buffer::Create(width, height)` が返す
`stride_y` / `stride_u` / `stride_v` の値は API 上の保証が無い。現状の libwebrtc (`m150.7871.3.0`) では
`stride_y == width`、`stride_u == stride_v == (width+1)/2` となり、実データが Y → U → V と連続配置される。
現状の一括書き込みは、この libwebrtc の実装詳細 (Y/U/V 連続・stride==width) に依存している。

## 設計方針

### FILE* リーク

`fopen` 直後に `file_.reset(fp, [](FILE* fp) { ::fclose(fp); });` を実行し、
その後で `file_size` 取得を行う。エラー時は RAII で自動 close される。

### fps_den_ 0 除算

`ReadHeader` の validation を `if (width_ == 0 || height_ == 0 || fps_num_ == 0 || fps_den_ == 0)` に拡張する。

### I420Buffer stride 前提

Y4M フレームは Y プレーン (`width*height`) → U プレーン → V プレーン (各 `(width+1)/2 * (height+1)/2`) の順に
連続格納されるため、読み出し側 (`Y4MReader`) は一括読み出しのままとする。stride 前提の解消は
書き込み先である `FakeVideoCapturer` 側で行う。

1. `GetFrame` の書き込み先を `GetSize()` バイトの一時バッファ (`std::vector<uint8_t>`) に変更する
2. 一時バッファから `y4m_buffer_` の `MutableDataY()` / `MutableDataU()` / `MutableDataV()` へ行単位でコピーする
   - コピー元の行幅: Y プレーンは `width`、U / V プレーンは `(width+1)/2`
   - コピー先のオフセット: 各プレーンの `StrideY()` / `StrideU()` / `StrideV()` を使う
   - `GetFrame` は同一フレームの再要求で `*updated = false` を返し、その場合一時バッファへ書き込まないため、
     コピーは `updated == true` のときのみ行う

libwebrtc の `I420Buffer::Create` が返す stride と Y/U/V の配置は API 上の保証が無いため、プレーンごとに stride を
扱う実装に修正する。`webrtc::I420Buffer` には stride を指定する 5 引数版 `Create` と protected の 5 引数コンストラクタが
あり、`stride_y == width` に固定すること自体は可能だが、`DataU()` / `DataV()` が `DataY()` の直後に連続配置される
ことは API 上保証されておらず、現行 libwebrtc の単一アロケーションとオフセット計算 (`DataU() = data_ + stride_y_ * height_`
など) への依存が残る。そのため一括書き込みの暗黙前提は構造的には解消されず、採用しない。

## 完了条件

- `Open` で `file_size` 取得失敗時に FILE* がリークしないこと (Valgrind は環境に無いため、
  `fopen` の直後に RAII へ載せる構造であることをコード上の保証として確認する)
- `fps_den_ == 0` の Y4M を渡した際に除算エラーではなくエラー返却で終わること
- Y4M フレームが `y4m_buffer_` のプレーン別 stride (`StrideY()` / `StrideU()` / `StrideV()`) を考慮した行単位コピーで書き込まれており、`I420Buffer` への `GetSize()` 一括書き込みと stride == width 前提がコードに残っていないこと

## 解決方法

`src/y4m_reader.cpp` と `src/fake_video_capturer.cpp` を次のように修正した。

- `Y4MReader::Open` は `fopen` の直後に `file_.reset(fp, ...)` を実行し、その後で
  `file_size` を取得する。取得に失敗しても RAII で閉じられる
- `Y4MReader::ReadHeader` の検証に `fps_den_ == 0` を追加する。`GetFrame` の
  `fps_num_ / (1000 * fps_den_)` で 0 除算になるため
- あわせて幅と高さの検証を `== 0` から `<= 0` に変更する。負値の場合は `GetSize()` が
  負値になり、`I420Buffer::Create` の `RTC_CHECK_GT(width, 0)` で abort するため
- 1 フレームの大きさが 1GiB を超える場合は拒否する。極端に大きい寸法は `GetSize()` の
  int 計算がオーバーフローするため
- `FakeVideoCapturer` は `Y4MReader::Open` の失敗を
  `Failed to Y4MReader::Open: path=... result=...` としてログに出す
- `FakeVideoCapturer` は `GetFrame` の書き込み先を `y4m_frame_buffer_`
  (`std::vector<uint8_t>`) に変更し、`CopyY4MFrameToI420Buffer()` で
  `StrideY()` / `StrideU()` / `StrideV()` を使った行単位コピーを行う。`GetFrame` が
  `*updated = false` を返した場合はバッファに書き込まないため、コピーもしない

検証したこと:

- `python3 run.py build macos_arm64` が成功する
- `Y4MReader` を直接呼ぶ一時的な検証で、`F30:0` の Y4M が `Open` で -9 を返すこと、
  正常な Y4M (`5x3`) が `GetSize() = 27` を返し、同じ時刻の再要求で `updated = false`
  になることを確認した
- 実バイナリに `--fake-video-capture` で異常な Y4M を渡しても 0 除算のシグナル
  (SIGFPE) で落ちないことを確認した
- `uv run pytest -q` が通る
- `clang-format -style=file` が `src/` の全ファイルで差分を出さない

`FakeVideoCapturer` は `Y4MReader::Open` の失敗をログに出さずに戻っていたため、
`Failed to Y4MReader::Open: path=... result=...` を出すようにした。これで異常な
ヘッダの Y4M が E2E でもエラーとして扱われたことを確認できる。

`test/test_readers.py` を追加した。`no-video-device` を指定すると capturer が作られず
Y4M の読み出しが実行されないため、Y4M のテストでは指定しない。検証するのは次の 2 点である。

- 異常なヘッダ (分母が 0、負の幅、負の高さ、極端に大きい寸法) の Y4M で
  `Failed to Y4MReader::Open` と該当する戻り値が出て、プロセスがシグナルで落ちないこと
- 奇数の幅と高さの Y4M を読み出してもプロセスがシグナルで落ちないこと

プレーン別 stride のコピーは、現行 libwebrtc が `stride_y == width` を返すため修正前と
同じ結果になり E2E では観測できない。コピーが各プレーンの stride を使っていることを
コード上の保証として確認する。

FILE* リークは Valgrind を必要とするため自動テストにしていない。`fopen` の直後に
RAII へ載せる構造にしたことをコード上の保証として確認する。

0 除算はプラットフォームによって挙動が異なる (x86_64 の整数 0 除算は SIGFPE になるが、
arm64 では 0 を返すため落ちない)。そのため「0 除算で落ちないこと」ではなく
「`Open` がエラーを返したこと」をログで検証する。`Y4MReader` を直接呼ぶ検証でも
`F30:0` で `Open` が -9 を返すことを確認した。

極端に大きい寸法の Y4M は `GetSize()` の int 計算がオーバーフローするため、1 フレームの
大きさの上限 (1GiB) も検証するようにした。

`CHANGES.md` の `## develop` に `[FIX]` のエントリを 2 件追加した。
