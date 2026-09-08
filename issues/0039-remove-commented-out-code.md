# コメントアウトされた古いコード・デバッグ痕跡の削除

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-commented-out-code
- Polished: 2026-09-08

## 目的

`src/` 配下の C++ コードにはコメントアウトされた古い実装やデバッグ用の痕跡が残っている。
git 履歴に残っているためコメントとして残しておく理由はなく、まとめて削除する。

## 現状

以下は `src/` を全数確認した結果であり、削除対象はこのリストに限定する。

### コメントアウトされた実装

- `src/game/game_key_core.h` の `//int PopKey() { ... }` (コメントアウトされた 9 行の実装)
- `src/fake_video_capturer.cpp` の Sandstorm デバッグ痕跡 (auto now = ..., auto now2 = ..., RTC_LOG(LS_INFO) << "sandstorm " ... のコメントアウトブロック、計 7 行)
- `src/fake_video_capturer.cpp` のフォントロード失敗時の `//printf("Failed to load a font-face (err=%u)\n", err);`
- `src/fake_video_capturer.h` の `//Random<uint32_t> random_{0, 256 * 256 * 256 - 1};`

### 過剰・自明なコメント

- `src/zakuro.cpp` の `ParseDataChannels` の `// boost::optional<bool> ordered;` などの型名とプロパティ名を書いただけの冗長なコメント (5 箇所)

## 設計方針

- 上記のコメントアウトを全て削除する。git 履歴から復元できるため、コメントとして残しておく理由はない
- 過剰・自明なコメントも削除する
- フォントロード失敗の `//printf("...")` は `RTC_LOG(LS_ERROR)` に置き換える
  - 現行のエラーパスは `if (err) { return; }` だけで無出力であり、フォントロードに失敗するとキャプチャスレッドが終了して映像が一切出ないため、エラー内容をログに残す
  - 同ファイルの Y4MReader エラーパス (`RTC_LOG(LS_ERROR)` + `return;`) と同じ形に合わせる
- 削除対象は 現状 のリストのみとする
  - `src/util.h` のマクロ使用例 (`// if (ec)` 等) と `src/main.cpp` の stats ファイルの JSON 例は説明用コメントとして残す
  - `src/y4m_reader.cpp` の chroma フォーマット説明と `src/zakuro_audio_device_module.h` の `//webrtc::AudioDeviceModule` は説明・見出しコメントとして残す
  - `buildbase.py` は melpon/buildbase のコピーテンプレートであり、更新時に上流から上書きされるため編集しない

## 完了条件

- 現状 に挙げたコメントアウトが全て削除されていること (計 23 行)
  - `src/game/game_key_core.h` の `//int PopKey()` からの 9 行
  - `src/fake_video_capturer.cpp` の Sandstorm デバッグ痕跡 7 行
  - `src/fake_video_capturer.cpp` のフォントロード失敗時の `//printf(...)` の 1 行
  - `src/fake_video_capturer.h` の `//Random<uint32_t> ...` の 1 行
  - `src/zakuro.cpp` の `// boost::optional<...>` の 5 行
- `grep -rnE '//int PopKey|//printf\("Failed|//auto now|//RTC_LOG\(LS_INFO\) << "sandstorm|//Random<uint32_t>|// boost::optional' src/` が 0 件であること
- フォントロード失敗時に `RTC_LOG(LS_ERROR)` でエラー内容がログに出ること
- コードを読むときに「なぜこれがコメントアウトされているか」を検討する必要が無くなること
- 削除後もビルド・テストが通ること (macOS arm64 / Ubuntu 22.04 / Ubuntu 24.04 のビルドと test_version)
