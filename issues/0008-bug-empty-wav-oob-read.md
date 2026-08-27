# 空 WAV データで ZakuroAudioDeviceModule のオーディオスレッドが OOB 読み出しする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-empty-wav-oob-read
- Polished: {YYYY-MM-DD}
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

以下のいずれか（推奨は両方）で対処する。

- `WavReader::Load` の末尾で `if (data.empty()) return <負のエラーコード>;` を返す。
  空 data チャンクの WAV は入力として不正なので、エラーで弾く
- `ZakuroAudioDeviceModule::StartAudioThread` の Safari / FakeAudio 分岐で
  `if (fake_audio_->data.empty()) { /* 無音を送る、または break */ }` のガードを追加

前者だけでも本 issue の UB は防げるが、後者を defense-in-depth として入れておくと将来の変更にも強くなる。

## 完了条件

- `WavReader::Load` が空 data チャンクの WAV を成功として受理しないこと（単体テストで検証可能）
- 空 data の `FakeAudioData` を `ZakuroAudioDeviceModule` に流し込んでも OOB 読み出しが発生しないこと
- AddressSanitizer 有効ビルドで空 WAV を `--fake-audio-capture` 指定して数秒動かし、OOB read が検知されないこと
