# Boost.JSON の unchecked アクセスで想定外入力の際に例外が伝播しクラッシュする

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-boost-json-unchecked-access
- Polished: {YYYY-MM-DD}

## 目的

コードベース全体で `boost::json::value::as_object()` / `.at()` / `.as_string()` などの
unchecked アクセスを try/catch せずに使っており、想定外の JSON 入力 (型不一致、フィールド欠落) で
`boost::json::system_error` が投げられて上位でキャッチされずスレッドやプロセスが飛ぶ経路を修正する。

## 現状

### 特に危険な箇所

- `src/virtual_client.cpp` の `OnNotify` で `json.at("event_type").as_string()` を無条件で呼ぶ。
  Sora からの notify 1 通が想定外の形式ならスレッドが飛ぶ
- `src/main.cpp` の `--config` JSONC 読み込みで `zakuro_obj.at(...)` / `.as_bool()` / `.as_object()` を多用
- `src/util.cpp` の `Util::ParseInstanceToArgs` で JSON 値の型を都度検査せず `as_object()` / `at()` を呼ぶ

いずれも例外を上位でキャッチしていない。

## 設計方針

- `virtual_client.cpp::OnNotify` は `if (json.is_object() && json.as_object().contains("event_type") && json.at("event_type").is_string())` の形でガードする
- `main.cpp` / `util.cpp` の JSON アクセスは try/catch で囲むか、`contains` / `is_*` でガードする
- ガードで failure を検知した場合は `RTC_LOG(LS_ERROR)` で英語メッセージを出し、
  設定エラーなら `main` に return してプロセス終了、runtime エラーなら該当通知を無視する
- 共通ヘルパー関数 (`SafeGet<T>(const boost::json::value&, const char*)`) を導入するのも検討

## 完了条件

- 全ての `.as_*()` / `.at()` の呼び出しに対して、事前ガードまたは try/catch がある
- 想定外の JSON 入力 (欠落、型不一致) を渡してもプロセス / スレッドが飛ばないこと (単体テストで検証)
- Sora からの想定外 notify 1 通で `VirtualClient` のスレッドが飛ばないこと
