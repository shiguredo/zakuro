# 空 WAV データで ZakuroAudioDeviceModule のオーディオスレッドが OOB 読み出しする

- Created: 2026-08-27
- Completed: 2026-09-30
- Branch: feature/fix-empty-wav-oob-read
- Polished: 2026-09-07
- Updated: 2026-09-29
- Milestone: 2026.1.0

## 目的

`WavReader::Load` が data チャンクサイズ 0 の入力を成功として受理してしまい、
`ZakuroAudioDeviceModule` のオーディオスレッドが空 vector に対して OOB 読み出しを実行する
未定義動作を修正する。

## 現状

`src/wav_reader.cpp` の `WavReader::Load` は data チャンクを読み込むループで `int n = chunk_size / 2;` を計算し、
`n == 0` のときは for が回らず `data` を空のまま `return 0;` する。

`--fake-audio-capture` 経由でこの空データが `FakeAudioData::data` に流し込まれると、
`src/zakuro_audio_device_module.cpp` の `StartAudioThread` のループで
`Type::Safari` / `Type::FakeAudio` 分岐が `fake_audio_->data[index]` を無条件に読み取る。
`index == 0` でも `data.empty()` なので OOB 読み出しになる。
その後の `if (index >= fake_audio_->data.size()) index = 0;` は事前チェックとして機能しないため、
最初の 1 サンプルで既に UB を踏む。

## 設計方針

以下の 2 つを実施する。片方だけでは完了条件を満たせない。

1. `WavReader::Load` が data チャンクを読み込んだ直後に `if (data.empty()) return -13;` を返す。
   空 data チャンクの WAV は入力として不正なので、エラーで弾く。
   `-13` は既存のエラーコード (`-1`, `-4` 〜 `-12`, `1`) と衝突しない新規コードとする。
2. `ZakuroAudioDeviceModule::StartAudioThread` の Safari / FakeAudio 分岐で、
   空 data を検出したら 0 (無音) を送るガードを追加する。
   10 ミリ秒分の無音バッファを生成し、通常どおり `SetRecordedBuffer` / `DeliverRecordedData` で送出する。
   録音デバイスとして無音を送出し続ける方が、受信側に音声の欠落を作らないため。

対処 1 だけでも本 issue の UB は防げるが、対処 2 は将来の変更に対する defense-in-depth であり、
空 data の `FakeAudioData` を直接構築するコード経路 (テスト等) があってもクラッシュしないことを保証する。

なお、対処 2 の検証は `--fake-audio-capture` 経由では行えない。対処 1 により空 data チャンクの WAV は
`WavReader::Load` で拒否されるため、空 data の `FakeAudioData` を直接構築して
`ZakuroAudioDeviceModule` に渡す検証が必要になる。
C++ 単体テスト基盤は撤去済みで、テストは実バイナリを起動する pytest に一本化されており、
この検証は pytest では行えない。対処 2 はコード上の保証として確認する。

## 完了条件

- `WavReader::Load` が空 data チャンクの WAV を成功として受理しないこと
  (実バイナリを起動する pytest の E2E で、空 data チャンクの WAV を `--fake-audio-capture` に
  指定したときに `failed to load fake audio: path=... result=-13` が標準エラー出力に出て
  終了コード 1 で終了することを確認する)
- 空 data の `FakeAudioData` を `ZakuroAudioDeviceModule` に流し込んでも OOB 読み出しが発生せず、無音が送出されること
  (空 data の `FakeAudioData` を直接構築する検証は C++ 単体テスト基盤が無いため pytest では行えない。
  また音声スレッドは Sora への接続が成立した後にしか開始されないため、実バイナリを起動する
  E2E でも到達できない。`fake_audio_->data` が空の場合に無音を送出する分岐があることを
  コード上の保証として確認する)
- 空 data チャンクの WAV を `--fake-audio-capture` に指定して起動したときに、`WavReader::Load` の
  エラーログが出力されてオーディオスレッドが開始されないこと
  (AddressSanitizer 有効ビルドの手段はリポジトリに無いため、サニタイザでの確認は本 issue の
  完了条件に含めない)

## 解決方法

`src/wav_reader.cpp` と `src/zakuro_audio_device_module.cpp` を次のように修正した。

- `WavReader::Load` は data チャンクを読み込んだ直後に `data.empty()` を確認し、空の場合は
  -13 を返す
- `ZakuroAudioDeviceModule::StartAudioThread` は `fake_audio_->data` が空の場合に
  無音のバッファを `SetRecordedBuffer` / `DeliverRecordedData` で送出する分岐を追加した。
  空のまま `data[index]` を読む経路をなくす
- あわせて添字を `(index + 1) % data.size()` に変更した。`data` の要素数が `channels` の
  倍数でない場合、従来は内側のチャンネルのループの途中で `data.size()` に達して
  範囲外を読んでいた
- `buf_size` (10 ミリ秒分の要素数) が 0 以下になる場合は音声スレッドを開始しない。
  剰余算と 0 除算を避けるため

検証したこと:

- `python3 run.py build macos_arm64` が成功する
- 空 data チャンクの WAV を `--fake-audio-capture` に指定すると
  `failed to load fake audio: path=... result=-13` を標準エラー出力に出して終了コード 1 で
  終了する。検査を外した実装では同じ入力でプロセスが応答しなくなる (テストが失敗する)
- `uv run pytest -q` が通る
- `clang-format -style=file` が `src/` の全ファイルで差分を出さない

`test/test_readers.py` に空 data チャンクの WAV を拒否する E2E テストを追加した。

対処 2 は、空 data の `FakeAudioData` を直接構築する C++ 単体テスト基盤が無いため E2E では
検証していない。`fake_audio_->data` が空の場合に無音を送出する分岐があり、空の vector に
添字アクセスしないことをコード上の保証として確認した。

`CHANGES.md` の `## develop` に `[FIX]` のエントリを追加した。
