# VirtualClient の状態遷移を明示化し bad_function_call / raw this を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-virtual-client-state-machine
- Polished: 2026-09-08

## 目的

`VirtualClient` は `closing_` / `need_reconnect_` / `on_close_` / `retry_count_` の暗黙の状態機械で動いており、
読解しづらいうえに以下の実害が発生する。

- `Close` の 2 段階呼び出しで `std::bad_function_call` を投げる経路がある
- `OnDisconnect` の `retry_timer_.async_wait` が raw `this` キャプチャで shared_from_this / weak_from_this を使っていない
- `Connect` と `Close` の再入で状態遷移がテスト可能な形になっていない

明示的な `enum class State` にリファクタし、上記経路を潰す。

## 現状

### bad_function_call の経路

`src/virtual_client.cpp` の `Close` には `on_close(...)` を無条件に invoke する箇所が 2 つある。

- `closing_` かつ `on_close_ != nullptr` のときは `on_close("already closing");` を呼ぶ
- `!closing_` かつ `signaling_ == nullptr` のときは `on_close("already closed");` を呼ぶ

どちらも引数の `std::function` にガードがなく、`on_close` は `virtual_client.h` で default `nullptr` のため、
引数無し `Close()` が呼ばれると空の `std::function` を invoke して `std::bad_function_call` を投げる。
`scenario_player.h` の OP_DISCONNECT が `Close()` を引数無しで呼ぶため、
接続状態と `on_close_` の登録状況の組み合わせ次第で踏みうる。

### raw this キャプチャ

`OnDisconnect` の `retry_timer_.async_wait([this](...) { ... Connect(); });` は raw `this` キャプチャ。
`VirtualClient` は `enable_shared_from_this` を継承しているのに `weak_from_this` を使っていない。

コールバックはメンバの `retry_timer_` に束縛されているため、`VirtualClient` の破棄と同時にハンドラも
破棄される。現時点のスコープでは発火しないが、`Connect()` は `shared_from_this()` を呼ぶため、
タイマーとオブジェクトの寿命が分離した瞬間に dangling する余地がある。
なお、`retry_timer_` が `io_context` より長寿命になる UB
(`issues/0007-bug-retry-timer-outlives-io-context.md` で対応) は本 issue の対象外とする。

### 状態機械の暗黙性

`Close` / `Connect` / `OnDisconnect` の相互作用が変数の組み合わせで動作しており、
どの状態遷移が有効かがコード全体を追わないと分からない。テストも書けない。

## 設計方針

- `enum class State { Idle, Connecting, Connected, Closing };` を導入し、遷移を明示化する
  - 状態を表す `closing_` は State に置き換える。`need_reconnect_` は「Closing 中の再接続要求」、
    `retry_count_` は「接続失敗回数のカウンタ」として残す
  - `SoraSignalingObserver` に接続完了を知るコールバックは存在しないため、`Connected` への
    遷移トリガーは offer 受信を意味する `OnSetOffer` とする
- `Close` の `on_close(...)` 呼び出しを `if (on_close) on_close(...);` でガードする
  (`already closing` / `already closed` の 2 経路。bad_function_call の即時対応)
- `OnDisconnect` の `async_wait` を `[weak = weak_from_this()](auto ec) { auto self = weak.lock(); if (!self) return; ... }` に変更する
- 状態遷移表を以下に明記し、実装時には同じ表をコード内のコメントにも残す

### 状態遷移表

| 遷移元 | イベント | 遷移先 | 補足 |
|---|---|---|---|
| Idle | `Connect()` | Connecting | `SoraSignaling` を作成して接続を開始する |
| Connecting | `OnSetOffer` | Connected | offer の受信をもって接続確立とする |
| Connecting / Connected | `Close(callback)` | Closing | `on_close_` を登録して `Disconnect()` する |
| Connecting / Connected | `Connect()` | Closing | `need_reconnect_ = true` にして `Disconnect()` する (再接続要求) |
| Connecting / Connected | 予期しない `OnDisconnect` | Idle | `retry_count_ < max_retry` なら `retry_timer_` で待機後に `Connect()` する |
| Closing | `OnDisconnect` | Connecting (`need_reconnect_`) / Idle | `on_close_` があれば通知する |

- `Closing` 中の `Close(callback)` は遷移せず、`on_close_` が空なら登録、登録済みなら
  `already closing` を通知する (ガードにより引数無し `Close()` は no-op)
- 状態遷移の単体テストを追加する (Sora SDK モック不要な範囲で。`Close` の 2 段階呼び出し、`Connect` 再入等)。
  C++ 単体テスト基盤 (GoogleTest / doctest / Catch2 のいずれか) は issues/0043 で整備予定のため、
  基盤導入後に追加する

## 完了条件

- `Close` を任意の順序で複数回呼んでも `std::bad_function_call` が投げられないこと
- `retry_timer_` のコールバックが raw `this` を保持しないこと
- 状態遷移表 (上記) の遷移が State として実装され、コード内のコメントにも明記されていること
- 状態遷移の代表的なシーケンスを覆う単体テストが追加されていること
  (C++ 単体テスト基盤は issues/0043 で整備予定のため、基盤導入後に追加する)
