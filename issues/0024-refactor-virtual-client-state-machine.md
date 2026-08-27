# VirtualClient の状態遷移を明示化し bad_function_call / raw this を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-virtual-client-state-machine
- Polished: {YYYY-MM-DD}

## 目的

`VirtualClient` は `closing_` / `need_reconnect_` / `on_close_` / `retry_count_` の暗黙の状態機械で動いており、
読解しづらいうえに以下の実害が発生する。

- `Close` の 2 段階呼び出しで `std::bad_function_call` を投げる経路がある
- `OnDisconnect` の `retry_timer_.async_wait` が raw `this` キャプチャで shared_from_this / weak_from_this を使っていない
- `Connect` と `Close` の再入で状態遷移がテスト可能な形になっていない

明示的な `enum class State` にリファクタし、上記経路を潰す。

## 現状

### bad_function_call の経路

`src/virtual_client.cpp` の `Close` は `closing_ && on_close_ != nullptr` のとき
`else` 分岐で `on_close("already closing");` を呼ぶ。ここに default `nullptr` の引数で `Close()` が呼ばれると、
空の `std::function` を invoke して `std::bad_function_call` を投げる。
`scenario_player.h` の OP_DISCONNECT は `Close()` を引数無しで呼ぶため、シナリオ次第で踏みうる。

### raw this キャプチャ

`OnDisconnect` の `retry_timer_.async_wait([this](...) { ... Connect(); });` は raw `this` キャプチャ。
`VirtualClient` は `enable_shared_from_this` を継承しているのに `weak_from_this` を使っていない。
現状の `Zakuro::Run` の scope 順序では大抵安全に動くが、将来の変更で dangling する余地がある。

### 状態機械の暗黙性

`Close` / `Connect` / `OnDisconnect` の相互作用が変数の組み合わせで動作しており、
どの状態遷移が有効かがコード全体を追わないと分からない。テストも書けない。

## 設計方針

- `enum class State { Idle, Connecting, Connected, Closing };` を導入し、遷移を明示化
- `Close` の分岐で `if (on_close) on_close(...);` のガードを追加 (bad_function_call の即時対応)
- `OnDisconnect` の `async_wait` を `[weak = weak_from_this()](auto ec) { auto self = weak.lock(); if (!self) return; ... }` に変更
- 状態遷移テーブルを issue またはコード内のコメントに明記
- 状態遷移の単体テストを追加 (Sora SDK モック不要な範囲で。`Close` の 2 段階呼び出し、`Connect` 再入等)

## 完了条件

- `Close` を任意の順序で複数回呼んでも `std::bad_function_call` が投げられないこと
- `retry_timer_` のコールバックが raw `this` を保持しないこと
- 状態遷移の代表的なシーケンスを覆う単体テストが追加されていること
