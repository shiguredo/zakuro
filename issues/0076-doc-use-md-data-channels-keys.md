# doc/USE.md の data-channels のキー名と JSON 例が実装と一致していない

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/update-use-md-data-channels-keys
- Polished: 2026-09-30

## 目的

`doc/USE.md` の data-channels の設定例を実装と完全に一致させ、利用者が例をそのまま
設定ファイルへ転記しても、実装が受け付けるキーと食い違わないようにする。
例に現れていない任意キーや別名キーがあると、利用者は設定方法を実装から推測するしか
無くなり、設定したつもりのオプションが無言で無視されることに気付けない。

## 現状

`doc/USE.md` の data-channels の例 (`### DataChannel メッセージングの設定`) には、
`label` / `direction` / `interval` / `size-min` / `size-max` が書かれ、
`ordered` / `max_packet_life_time` / `max_retransmits` はコメントで示されている。
`protocol` / `compress` は例に現れていない。

キー名の不一致は解消済みである。2026-09-30 のコミット
(2026.1.0 向けのドキュメント修正、コミット
`1465631219bb9eca8b6b6b9f9ed80861d1ea254f`) を受けて、`doc/USE.md` の例は
`size_min` → `size-min`、`size_max` → `size-max`、`max_packet_lifetime` →
`max_packet_life_time` に修正されており、例に書かれたキー名はすべて実装が受理する。
また、例の `"size-max"` の行の後にカンマが無くても、データチャンネルのオブジェクトの
末尾のプロパティでありコメント除去後に有効な JSON になるため、JSONC として妥当である
(コメントと末尾カンマを許可する `Util::LoadJsoncFile` の `parse_options` でも受理される)。

例に対する残りの課題は次のとおり。

- 別名 (`size_min` / `size_max`) も受理されることが `doc/USE.md` に記載されていない
- 任意キーの一覧が実装と一致していない (`protocol` / `compress` が例に現れない)
- キー名の一覧と既定値が 1 箇所にまとまっていない

確認したこと:

- `src/zakuro.cpp` の `ParseDataChannels` は `size-min` → `size_min`、`size-max` →
  `size_max` の順に探索し、`max_packet_life_time` は別名を持たない
- 未知名は無視されるため、`max_packet_lifetime` を指定しても警告もエラーも出ない
  (トップレベルの未知キーの検出は open issue 0059 の対象であり、インスタンス配下の
  未知キーの検出は 0059 の対象外のままである)
- 実装が受け付けるキー名は `label` / `direction` / `interval` / `size-min` / `size-max` /
  `ordered` / `max_packet_life_time` / `max_retransmits` / `protocol` / `compress` であり、
  このうち `size_min` / `size_max` は別名としても受理される
- 既定値は `src/zakuro.cpp` の `DataChannels::Channel` の初期値どおり、`interval` が
  500 (ms)、`size-min` / `size-max` が 48 (bytes) であり、両サイズの有効範囲は
  `MESSAGE_SIZE_MIN` (48) 〜 `MESSAGE_SIZE_MAX` (256000)

## 設計方針

- 例では正規名 (`size-min` / `size-max` / `max_packet_life_time`) だけを使用し、
  別名の存在は `doc/USE.md` の注記に留める (キー名の修正は済んでいるため、
  注記の追加が残作業となる)
- 別名 (`size_min` / `size_max`) も受理する旨を `doc/USE.md` の data-channels 節に
  注記する
- 例に現れていない任意キー (`protocol` / `compress`) をコメントとして追加する
- キー名の一覧と既定値を `doc/USE.md` の data-channels 節内の 1 箇所にまとめ、
  実装と突き合わせやすい形にする

## 完了条件

- 例に書かれたキー名がすべて実装の受理するキー名と一致していること
- 例の全体が JSONC として妥当であること
- 別名キーの扱いが記述されていること
- 任意キー (`ordered` / `max_packet_life_time` / `max_retransmits` / `protocol` /
  `compress`) の一覧が実装と一致していること

## 解決方法

{YYYY-MM-DD} に記入
