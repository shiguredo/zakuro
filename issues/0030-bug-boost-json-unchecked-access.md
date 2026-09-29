# Boost.JSON の unchecked アクセスで想定外入力の際に例外が伝播しクラッシュする

- Created: 2026-08-27
- Completed: 2026-09-29
- Branch: feature/fix-boost-json-unchecked-access
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

コードベース全体で `boost::json::value::as_object()` / `.at()` / `.as_string()` などの
unchecked アクセスを try/catch せずに使っており、想定外の JSON 入力 (型不一致、フィールド欠落) で
`boost::json::system_error` が投げられて上位でキャッチされずスレッドやプロセスが飛ぶ経路を修正する。

## 現状

unchecked アクセスによって例外が伝播し得る箇所は次の 4 ファイルに限られる。

### 特に危険な箇所

- `src/virtual_client.cpp` の `OnNotify` で `boost::json::parse(text)` と
  `json.at("event_type").as_string()` を無条件に呼ぶ。
  Sora からの notify 1 通が JSON として不正、または想定外の形式 (フィールド欠落、型不一致) なら
  例外が伝播しスレッドが飛ぶ
- `src/main.cpp` の `--config` JSONC 読み込みでは、JSON ルートがオブジェクトでない場合の
  `zakuro_value.as_object()`、`ui` が bool でない場合の `zakuro_obj.at("ui").as_bool()`、
  `instances` が配列でない場合の `zakuro_obj.at("instances").as_array()` が unchecked。
  `zakuro_obj.at("log-level")` などのその他の `.at()` は `.contains()` によるキー欠落チェックは
  あるが、値の型は未検査
- `src/util.cpp` の `Util::ParseInstanceToArgs` で `inst.as_object()`、
  `boost::json::value_to<int>(obj.at("instance-num"))` (キー欠落チェックはあるが型は未検査)、
  `sora` の値の `.as_object()` が unchecked。
  さらに `sora.signaling-url` の型が不正な場合は `throw std::runtime_error` を投げるが、
  呼び出し元の `main` で catch されていない。
  `Util::LoadJsoncFile` も拡張子不正・ファイルオープン失敗・JSON パース失敗で
  `throw std::runtime_error` を投げるが、呼び出し元の `main` (`--config` 読み込み) で
  catch されていないため、不正な JSONC を渡すだけで例外でプロセスが落ちる
- `src/zakuro.cpp` の `ParseDataChannels` は `ordered`、`max_packet_life_time`、
  `max_retransmits`、`protocol`、`compress` の `boost::json::value_to<>()` (型未検査) が unchecked。
  `ParseDataChannels` は `Zakuro::Run` から呼ばれ、`Zakuro::Run` は `main` のスレッド内で
  try/catch されていないため、data-channels 設定の型が想定外だと例外でプロセスが落ちる

いずれも例外を上位でキャッチしていない。

なお、`src/json_rpc.cpp`、`src/http_server.cpp` は型チェック済みであり、`src/util.cpp` の
`boost::json::value_from(...)` 起点の `.as_string()` / `.as_object()` は値の型がコード上で
確定しているため、いずれも想定外入力で例外にならず対象外とする。

## 設計方針

- `virtual_client.cpp::OnNotify` は `boost::json::parse` を try/catch で囲むか error_code 版で呼び、
  その後で `if (json.is_object() && json.as_object().contains("event_type") &&
  json.at("event_type").is_string())` の形でガードする
- `main.cpp` / `util.cpp` / `zakuro.cpp` の JSON アクセスは try/catch で囲むか、`contains` / `is_*` でガードする。
  `Util::ParseInstanceToArgs` はエラーを返り値 (bool 等) で返す方式に変更し、
  `sora.signaling-url` の型不正の `throw std::runtime_error` も併せて置き換える。
  `ParseDataChannels` は `ordered` 等の任意キーも `is_bool()` / `is_number()` / `is_string()` で
  ガードし、型不正なら既存の `label` 等と同じく `false` を返す。
  `Util::LoadJsoncFile` の `throw std::runtime_error` (拡張子・オープン・パース失敗) は、
  `main` 側で try/catch して設定エラーとして終了させるか、エラー返却方式に変更する
- ガードで failure を検知した場合、設定読み込み段階 (webrtc のログ初期化前) は既存コード同様
  `std::cerr` で英語メッセージを出し、設定エラーなら `main` から return 1 してプロセスを終了させる。
  `RTC_LOG(LS_ERROR)` はログ初期化後の runtime エラー報告にのみ使う
- 共通ヘルパー関数 (`SafeGet<T>(const boost::json::value&, const char*)`) を導入するのも検討

## 完了条件

- 変更対象 4 ファイル (`src/virtual_client.cpp`、`src/main.cpp`、`src/util.cpp`、`src/zakuro.cpp`) の
  unchecked な Boost.JSON アクセスが全て、事前ガードまたは try/catch で処理され、例外が上位へ
  伝播しないこと
- 想定外の JSON 入力 (不正 JSON、フィールド欠落、型不一致) を渡してもプロセス / スレッドが
  例外で飛ばないこと。設定読み込み経路は `test/` の pytest E2E で検証する
  (`--config` に不正型・欠落・不正 JSON の JSONC を渡し、プロセスが例外で落ちずに
  エラー終了することを確認する)
- Sora からの想定外 notify 1 通で `VirtualClient` のスレッドが飛ばないこと。
  想定外の notify を E2E で注入する手段が無いため、`OnNotify` のガード実装とコードレビューで担保する

## 解決方法

Boost.JSON の unchecked アクセスを、事前の型検査と例外の捕捉で保護した。

### 設定ファイル (JSONC) の読み込み

- `src/main.cpp` の `Util::LoadJsoncFile` 呼び出しを try/catch で囲み、拡張子不正・ファイルオープン失敗・
  JSON パース失敗の場合は `std::cerr` に英語のメッセージを出力して `return 1` するようにした
- ルートがオブジェクトでない場合と `instances` が配列でない場合を型検査で検出し、設定エラーとして
  `return 1` するようにした
- トップレベルの共通オプション (`log-level` / `http-port` / `http-host` /
  `output-file-connection-id` / `instance-hatch-rate`) の値がオブジェクトまたは配列の場合は
  設定エラーにした。従来は空文字列や未指定として扱われ、検証の無いオプションでは設定ミスが
  無言で通っていた
- `src/util.cpp` の `Util::ParseInstanceToArgs` の戻り値を `std::optional` に変更し、
  エラーを例外ではなく戻り値で返すようにした。あわせて `inst` / `instance-num` / `sora` /
  `sora.signaling-url` の型を検査し、`instance-num` が 0 以下または 1000 を超える場合と、
  `sora.signaling-url` が空配列の場合もエラーにした

### DataChannels の解析

- `src/zakuro.cpp` の `ParseDataChannels` で、`ordered` / `compress` は `is_bool()`、
  `protocol` は `is_string()` で型を検査してから `value_to` するようにした
- `interval` / `size-min` / `size-max` / `max_packet_life_time` / `max_retransmits` は
  `is_number()` だけでは不十分だった (`value_to<int>` は整数でない値と範囲外の値でも例外を投げる) ため、
  例外を投げない `boost::json::try_value_to` と `has_error()` に置き換えた

### Sora からの通知

- `src/virtual_client.cpp` の `OnNotify` を try/catch で囲み、`is_object()` /
  `contains("event_type")` / `is_string()` でガードしたうえで `as_string()` を使うようにした。
  想定外の通知は `RTC_LOG(LS_WARNING)` を出力して無視する

### テスト

- `test/test_config_json.py` を追加した。不正な JSONC (不正 JSON・型不一致・キー欠落・
  整数でない値・範囲外の値・空配列・拡張子不正) を渡して、シグナルで落ちずにエラー終了することを
  44 ケースで検証する。DataChannels の型不正、CLI11 の検証エラーの終了コード、
  正しい設定が CLI 引数へ正しく変換されて CLI11 の検証を通ること (値付きオプションの値トークン) も
  あわせて検証する

### issue 本文の記述の訂正

「現状」に挙げられていた `src/main.cpp` の `zakuro_obj.at("ui").as_bool()` は、UI リバースプロキシの
廃止に伴い既に存在しない。残る unchecked アクセスは「現状」に挙げられた箇所で全て対応した。

### 完了条件の確認

- 変更対象 4 ファイルの unchecked な Boost.JSON アクセスは全て事前ガードまたは try/catch で
  処理され、例外が上位へ伝播しない (`.at()` / `as_*()` / `value_to<>()` / `parse()` を列挙して確認)
- 想定外の JSON 入力を渡してもプロセスが例外で落ちないことを pytest E2E で確認した。
  修正前は終了コード 134 で SIGABRT していた入力が、いずれもエラーメッセージを出力して終了する
- `OnNotify` のガードは想定外の notify を E2E で注入する手段が無いため、実装とコードレビューで
  担保した
