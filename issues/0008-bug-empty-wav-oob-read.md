# 空 WAV データで ZakuroAudioDeviceModule のオーディオスレッドが OOB 読み出しする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-empty-wav-oob-read
- Polished: 2026-09-07
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

1. `WavReader::Load` が data チャンクを読み込んだ直後に `if (data.empty()) return -11;` を返す。
   空 data チャンクの WAV は入力として不正なので、エラーで弾く。
   `-11` は既存のエラーコード (`-1`, `-4` 〜 `-10`, `1`) と衝突しない新規コードとする。
2. `ZakuroAudioDeviceModule::StartAudioThread` の Safari / FakeAudio 分岐で、
   空 data を検出したら 0 (無音) を送るガードを追加する。
   10 ミリ秒分の無音バッファを生成し、通常どおり `SetRecordedBuffer` / `DeliverRecordedData` で送出する。
   録音デバイスとして無音を送出し続ける方が、受信側に音声の欠落を作らないため。

対処 1 だけでも本 issue の UB は防げるが、対処 2 は将来の変更に対する defense-in-depth であり、
空 data の `FakeAudioData` を直接構築するコード経路 (テスト等) があってもクラッシュしないことを保証する。

なお、対処 2 の検証は `--fake-audio-capture` 経由では行えない。対処 1 により空 data チャンクの WAV は
`WavReader::Load` で拒否されるため、空 data の `FakeAudioData` を直接構築して
`ZakuroAudioDeviceModule` に渡すテストが必要になる (C++ 単体テスト基盤は issue 0043 で整備予定)。

## 完了条件

- `WavReader::Load` が空 data チャンクの WAV を成功として受理しないこと（単体テストで検証可能）
- 空 data の `FakeAudioData` を `ZakuroAudioDeviceModule` に流し込んでも OOB 読み出しが発生せず、無音が送出されること
- AddressSanitizer 有効ビルドで空 data チャンクの WAV を `--fake-audio-capture` に指定して起動し、
  `WavReader::Load` のエラーログが出力されてオーディオスレッドが開始されず、OOB read が検知されないこと
