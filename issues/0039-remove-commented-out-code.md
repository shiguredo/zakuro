# コメントアウトされた古いコード・デバッグ痕跡の削除

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-commented-out-code
- Polished: {YYYY-MM-DD}

## 目的

コードベース全体にコメントアウトされた古い実装やデバッグ用の痕跡が残っている。
git 履歴に残っているため markdown コメントとして残しておく理由はなく、まとめて削除する。

## 現状

### コメントアウトされた実装

- `src/game/game_key_core.h` の `//int PopKey() { ... }` (コメントアウトされた 9 行の実装)
- `src/fake_video_capturer.cpp` の Sandstorm デバッグ痕跡 (auto now = ..., auto now2 = ..., RTC_LOG(LS_INFO) << "sandstorm " ... のコメントアウトブロック)
- `src/fake_video_capturer.cpp` のフォントロード失敗時の `//printf("Failed to load a font-face (err=%u)\n", err);`
- `src/fake_video_capturer.h` の `//Random<uint32_t> random_{0, 256 * 256 * 256 - 1};`

### 過剰・自明なコメント

- `src/zakuro.cpp` の `ParseDataChannels` の `// boost::optional<bool> ordered;` などの JSON プロパティ名を書いただけの自明コメント (5 箇所)

## 設計方針

- 上記のコメントアウトを全て削除する。git blame で復元したいなら履歴から辿れる
- 過剰・自明なコメントも削除する
- `//printf("...")` のようにログしたい意図があるなら `RTC_LOG(LS_ERROR)` に置き換えるか、明示的なエラー返却にする

## 完了条件

- `grep -rn '//.*[a-zA-Z]' src/ | grep -E '//\s*[A-Z].*\{|//\s*[a-z]+\(\)'` などで痕跡が減っていること
- コードを読むときに「なぜこれがコメントアウトされているか」を検討する必要が無くなること
- 削除後もビルド・テストが通ること
