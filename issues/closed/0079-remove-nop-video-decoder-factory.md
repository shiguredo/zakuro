# 未使用の NopVideoDecoderFactory を削除する

- Created: 2026-09-30
- Completed: 2026-09-30
- Branch: feature/remove-nop-video-decoder-factory
- Polished: {YYYY-MM-DD}

## 目的

`src/nop_video_decoder.h` の `NopVideoDecoderFactory` は現在どこからも使われていない。
Zakuro の中核である「受信した映像をデコードせずに捨てる」機構は今も生きているが、
その載せ方が Sora C++ SDK の VideoCodecPreference に移り、
`webrtc::VideoDecoderFactory` を自前実装したこのクラスは取り残されている。
未使用コードを削除し、`GetSupportedFormats` が引きずっている重い include も落とす。

## 現状

Zakuro のデコード破棄は次の経路で実現されている。

- `context_config.video_codec_factory_config.capability_config.get_custom_engines` が
  `kCustom_1` の独自エンジンを `custom_engine_name = "NopVideoDecoder"` として登録し、
  5 コーデックを encoder = false / decoder = true にする
- preference の構築で `kCustom_1` を全デコーダーに上書きする
- `context_config.video_codec_factory_config.create_video_decoder` のラムダが
  `kCustom_1` のときに `std::make_unique<NopVideoDecoder>()` を返す
- `NopVideoDecoder::Decode` は入力をデコードせず 320x240 の `I420Buffer` を
  `callback_->Decoded` に流す

いずれも `NopVideoDecoder` 本体だけを使い、`NopVideoDecoderFactory` は通らない。

`src/` / `test/` / `CMakeLists.txt` を検索しても `NopVideoDecoderFactory` の出現は
`src/nop_video_decoder.h` の宣言と `src/nop_video_decoder.cpp` の定義の 3 箇所だけで、
`src/` に `video_decoder_factory` を直接設定する箇所は無い。

参照が消えたのは `3a8d05e`「NopVideoDecoder を VideoCodecPreference の仕組みに乗せる」
(2025-04-23) で、それ以前は
`dependencies.video_decoder_factory.reset(new NopVideoDecoderFactory());` として
WebRTC の依存に直接差し込んでいた。

`NopVideoDecoderFactory::GetSupportedFormats` は H264 / VP8 / VP9 / AV1 のヘッダ
(`modules/video_coding/codecs/...`) を使っており、この関数が現在の唯一の利用箇所に
なっている。

## 設計方針

`NopVideoDecoderFactory` の宣言と定義を削除する。あわせて `src/nop_video_decoder.cpp`
から `GetSupportedFormats` の実装でのみ使われている include を削除する。

`NopVideoDecoder` 本体は `create_video_decoder` から使われているため残す。

issues/0038 は未使用シンボルと include の棚卸しを扱っているが、
`NopVideoDecoderFactory` はその一覧に含まれていない。

## 完了条件

- `NopVideoDecoderFactory` の宣言と定義が削除されていること
- `git grep -n 'NopVideoDecoderFactory'` の結果が 0 件になること
- `NopVideoDecoder` 本体は残り、`src/zakuro.cpp` の `create_video_decoder` が
  引き続き `NopVideoDecoder` を返すこと
- `GetSupportedFormats` でしか使っていない include が削除されていること
- ビルドに成功し、既存のテストが通ること

## 解決方法

`src/nop_video_decoder.h` から `NopVideoDecoderFactory` の宣言を、
`src/nop_video_decoder.cpp` から `GetSupportedFormats` と `Create` の定義を削除した。
`NopVideoDecoder` 本体は `create_video_decoder` から使われているため残している。

あわせて `GetSupportedFormats` の実装でしか使っていなかった include
(`media/base/media_constants.h`、`modules/video_coding/codecs/` 配下の av1 / h264 /
vp8 / vp9) と、Factory の基底クラスでしか使っていなかった
`api/video_codecs/video_decoder_factory.h` を削除した。

検証したこと:

- `git grep -n 'NopVideoDecoderFactory'` が `src/` で 0 件になる
- `NopVideoDecoder` 本体は残り、`src/zakuro.cpp` の `create_video_decoder` が
  引き続き `NopVideoDecoder` を返す
- `python3 run.py build macos_arm64` が成功する
- `uv run pytest -q` が 115 passed / 1 skipped で通る
- `clang-format -style=file` が `src/` の全ファイルで差分を出さない

`CHANGES.md` の `## develop` の `### misc` に `[UPDATE]` のエントリを追加した。
