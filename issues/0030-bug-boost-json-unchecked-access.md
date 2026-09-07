# Boost.JSON の unchecked アクセスで想定外入力の際に例外が伝播しクラッシュする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-boost-json-unchecked-access
- Polished: 2026-09-07

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
