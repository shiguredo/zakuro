# 映像と音声の Content Hint を指定できるようにする

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-content-hint
- Polished: {YYYY-MM-DD}

## 目的

送信する映像トラックと音声トラックに Content Hint を指定できるようにする。
Content Hint はトラックの内容 (映像なら動き / 詳細 / テキスト、音声なら音声 / 音声認識 / 音楽) を
ヒントとして伝えるもので、劣化制御や音声処理をコンテンツに適した挙動へ切り替えるために使う。
現状は映像の Content Hint を `--fixed-resolution` で `kText` に固定する手段しかない。

momo と同じ引数を用意する。
`--fixed-resolution` を廃止する破壊的変更を含む。

## 現状

- `src/util.cpp` の `Util::ParseArgs` が `--fixed-resolution` を定義し、`ZakuroConfig::fixed_resolution` に格納する。
- `src/zakuro.cpp` の `Zakuro::Run` が `VirtualClientConfig::fixed_resolution` に転送する。
- `src/virtual_client.cpp` の `VirtualClient::Connect` が、`fixed_resolution` が true のときだけ
  `video_track_->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kText)` を呼ぶ。
  値が `kText` に固定されており、他の Content Hint は指定できない。
- 同じ `VirtualClient::Connect` で音声トラックも作るが、`webrtc::AudioOptions` を組み立てるだけで
  Content Hint に相当する指定は無い。
- 同梱 libwebrtc (`DEPS` の `WEBRTC_BUILD_VERSION`) の `webrtc::VideoTrackInterface` には
  `ContentHint { kNone, kFluid, kDetailed, kText }` と `set_content_hint` がある。
  一方 `AudioTrackInterface` に Content Hint の API は無い。ブラウザは Blink の
  `MediaStreamAudioTrack` / `WebMediaStreamTrack::ContentHintType` 側で処理しており、
  ネイティブの公開 API には出ていない。
- sora-cpp-sdk にも Content Hint を設定する項目は無い。

## 設計方針

momo と同じ引数を追加する。

- `--video-content-hint` を追加する。値は `none` / `motion` / `detail` / `text` とし、
  `webrtc::VideoTrackInterface::ContentHint` の `kNone` / `kFluid` / `kDetailed` / `kText` に対応させる。
  指定が無い場合は `kNone` (現状と同じ) とする。
- `--audio-content-hint` を追加する。値は `speech` / `speech-recognition` / `music` とする。
  ネイティブ libwebrtc には audio の Content Hint API が無いため、`webrtc::AudioOptions` など
  Zakuro の音声処理で有効な設定にマッピングしてブラウザ相当の挙動を実現する。どの設定で
  ブラウザ相当になるか (エコーキャンセラ / ノイズサプレッション / 自動利得制御の切り替え等) は
  実装時に確認する。指定が無い場合は現状と同じ音声処理とする。
- `--fixed-resolution` を廃止する。このフラグは映像 Content Hint を `kText` に固定するだけなので、
  `--video-content-hint text` で代替できる。
- 上記に合わせて `ZakuroConfig` / `VirtualClientConfig` と JSONC 設定 (`fixed-resolution`) も変更する。

## 完了条件

- `--video-content-hint` の指定に応じて送信映像トラックの Content Hint が変わること
- `--audio-content-hint` の指定に応じて送信音声の処理設定が変わること
- `--fixed-resolution` が廃止され、指定しても受け付けないこと
- どちらのオプションも未指定なら現状と同じ動作になること

## 解決方法

(実装時に記入)
