# ParseDataChannels が int の範囲外の値で「整数でない」と出力する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-parse-data-channels-int-range-message
- Polished: 2026-09-30

## 目的

`src/zakuro.cpp` の `ParseDataChannels` は、数値のオプションを `boost::json::try_value_to<int>`
または `boost::json::try_value_to<int32_t>` で整数に変換し、失敗した場合は
「整数でなければならない」というメッセージをログに出す。
しかしこの変換は「整数でない」場合と「整数だが `int` (または `int32_t`) の範囲外」の場合を
区別しないため、範囲外の値に対して原因と食い違うメッセージが出る。

不正な設定を渡した利用者はログから原因を特定するため、値が整数として表現できないのか、
表現できるが範囲外なのかを区別できる必要がある。

## 現状

対象は `interval` / `size-min` / `size-max` / `max_packet_life_time` / `max_retransmits` の
5 項目で、いずれも `try_value_to<int>` または `try_value_to<int32_t>` の失敗を
「整数でなければならない」として扱っている。

確認したこと (実バイナリで実測):

- `interval` に `1.5` を指定した場合と `1e30` を指定した場合のどちらも
  `ParseDataChannels: interval must be an integer` と出力される
- `size-min` に `1e30` を指定した場合は `ParseDataChannels: size-min must be an integer`、
  `max_packet_life_time` に `1e30` を指定した場合も同じく「整数でない」と出力される
- `1e30` は JSON では整数値として表せる数値であり、実際の原因は `int` の範囲外である
- `size-min` / `size-max` には別途 `MESSAGE_SIZE_MIN` 〜 `MESSAGE_SIZE_MAX` の範囲チェックが
  あり、そちらは `size-min out of range: <値>` と報告しているため、同じ「範囲外」でも
  メッセージの形式が 2 種類に分かれている
- `boost::json::try_value_to<int>` は非整数値と範囲外の値で同じエラーを返すため、
  現行の実装では原因を区別できない
- `test/test_config_json.py` には `max_packet_life_time` の範囲外 (`1e30`) のケースがあり、
  `max_packet_life_time must be an integer` を期待値として固定している。テストのコメントは
  「int32_t の範囲外」と書かれており、期待値の文言と食い違っている

## 設計方針

- `try_value_to` の失敗を 2 種類に分けて報告する
  - 値が整数として表現できない場合: 「整数でなければならない」というメッセージ (現行どおり)
  - 値は整数として表現できるが対象の型の範囲外の場合: 「範囲外」であることと実際の値を含む
    メッセージ
- 値の表現は `boost::json::value` の種別 (`is_int64` / `is_uint64` / `is_double` など) と
  実際の値から判定する
- 範囲を判定するために、`int` ではなく `int64_t` / `uint64_t` へ変換してから対象の型に
  収まるかを確認する方法も検討する
- `size-min` / `size-max` の `out of range: <値>` と同じ形式に揃え、5 項目で一貫させる
- メッセージの変更に合わせて `test/test_config_json.py` の期待値を更新し、各項目に範囲外の
  ケースを追加する

## 完了条件

- 5 項目すべてで、非整数の値と範囲外の整数値が異なるメッセージとして出力されること
- 範囲外の場合は実際の値がメッセージに含まれること
- `test/test_config_json.py` の data-channels のテストが、非整数と範囲外の両方のケースを
  検証していること (期待値がメッセージと一致し、コメントと矛盾しないこと)
- `CHANGES.md` の `## develop` の `### misc` に `[UPDATE]` のエントリが追加されていること
- `python3 run.py build macos_arm64` などのビルドが通ること

## 解決方法

{YYYY-MM-DD} に記入
