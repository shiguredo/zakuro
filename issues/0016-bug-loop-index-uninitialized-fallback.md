# Zakuro::Run の loop_index が未初期化のまま使われる分岐がある

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-loop-index-uninitialized-fallback
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` の `int loop_index;` が特定の条件でどの分岐にも入らず、
未初期化のまま `const int li = loop_index + 1;` に使用される未定義動作の経路を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` は `int loop_index;` を宣言後、以下の 3 分岐でのみ代入している。

- `!fake_audio_key_trigger` の場合
- `fake_audio_key_trigger && config_.scenario == ""` の場合
- `fake_audio_key_trigger && config_.scenario == "reconnect"` の場合

現状の CLI validator では `scenario` は `""` と `"reconnect"` に絞られているため実質的には全パス代入されるが、
言語仕様上は「else が無い if / else if チェーン」で `loop_index` は未初期化。
将来 `scenario` の許容値が増えた際・JSONC 経由の別ルートが増えた際・Python バインディング等の別 entrypoint が加わった際に
未初期化のまま `const int li = loop_index + 1;` に流れ UB になる。

## 設計方針

以下 2 点を修正する。

- 宣言時に `int loop_index = 0;` で初期化する
- if / else if チェーンの末尾に `else { std::cerr << "unsupported scenario: " << config_.scenario << std::endl; return 1; }` を追加する
  (メッセージは AGENTS.md の規約に従い英語で出す)

これで CLI validator と実装の想定を明示的に一致させ、想定外の scenario に対するフォールバックが安全に。

## 完了条件

- `loop_index` が宣言時に初期化されていること
- 3 分岐のいずれにも該当しない場合に明確なエラーメッセージを出して非ゼロ終了すること
- `-Wuninitialized` 相当を有効にしてもコンパイル警告が出ないこと
