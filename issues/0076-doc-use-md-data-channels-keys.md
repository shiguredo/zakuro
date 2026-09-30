# doc/USE.md の data-channels のキー名と JSON 例が実装と一致していない

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/update-use-md-data-channels-keys
- Polished: {YYYY-MM-DD}

## 目的

`doc/USE.md` の data-channels の設定例に、実装が受け付けないキー名と、JSONC として壊れた
記述が含まれている。利用者が例をそのまま使うと、設定したつもりのオプションが無言で無視される。

## 現状

`doc/USE.md` の data-channels の例には次のキーが書かれている。

- `size_min` / `size_max`: 実装は `size-min` / `size-max` を先に探し、見つからない場合の
  別名として `size_min` / `size_max` も受理するため、この表記でも動作する
- `max_packet_lifetime`: 実装が探すのは `max_packet_life_time` のみであり、
  `max_packet_lifetime` は一致しないため無言で無視される
- `size_max` の行の後にカンマが無く、次のコメント行へ続くため、例の全体が JSONC として
  妥当でない
- `max_packet_life_time` 以外の任意キー (`ordered` / `max_retransmits` / `compress`) は
  コメントとして書かれており、`compress` は例に現れない

確認したこと:

- `src/zakuro.cpp` の `ParseDataChannels` は `size-min` → `size_min`、`size-max` →
  `size_max` の順に探索し、`max_packet_life_time` は別名を持たない
- 未知名は無視されるため、`max_packet_lifetime` を指定しても警告もエラーも出ない
  (トップレベルとインスタンス配下の未知キーの検出は issue 0059 の対象)
- 実装が受け付けるキー名は `label` / `direction` / `interval` / `size-min` / `size-max` /
  `ordered` / `max_packet_life_time` / `max_retransmits` / `protocol` / `compress` であり、
  このうち `size_min` / `size_max` は別名としても受理される

## 設計方針

- 例を実装が受け付けるキー名 (`size-min` / `size-max` / `max_packet_life_time`) で書き直す
- 別名 (`size_min` / `size_max`) を受け付けることを明記するか、例では正規名だけを使い、
  別名の存在は注記に留める
- `size_max` の行にカンマを補い、例の全体を JSONC として妥当にする
- 例に現れていない任意キー (`protocol` / `compress`) を追加するか、一覧として示す
- キー名の一覧と既定値を 1 箇所にまとめ、実装と突き合わせやすい形にする

## 完了条件

- 例に書かれたキー名がすべて実装の受理するキー名と一致すること
- 例の全体が JSONC として妥当であること
  (無理なら、書き直した例を実装に渡して受理されることを確認する)
- 別名キーの扱いが記述されていること
- 任意キー (`ordered` / `max_packet_life_time` / `max_retransmits` / `protocol` /
  `compress`) の一覧が実装と一致していること

## 解決方法

{YYYY-MM-DD} に記入
