# ScenarioPlayer のライフタイム管理と op 遷移を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-scenario-player-lifetime-and-state
- Polished: {YYYY-MM-DD}

## 目的

`ScenarioPlayer` に以下の問題があり、シナリオが仕様通りに進まない / 破棄後のオブジェクトを触るリスクを孕む。
まとめて修正する。

- `DoNext` / `OnNext` の `boost::asio::post` / `async_wait` が raw `this` キャプチャ
- OP_EXIT の case で `return` せず末尾の `Next(client_id)` に落ちて次 op が実行される
- OP_DISCONNECT / OP_RECONNECT を投げっぱなしで完了を待たずに次 op に進む
- `Play(client_id, ...)` を既に動作中の client に対して呼ぶと、既存の DoNext post が残って二重発火する
- OpSleep の `engine_() % (op.max_time_ms - op.min_time_ms + 1)` は `min > max` のとき UB (剰余の第二引数が非正)

## 現状

`src/scenario_player.h` の該当箇所。

- `DoNext` は `boost::asio::post(*config_.ioc, std::bind(&ScenarioPlayer::OnNext, this, client_id));` として raw `this` を bind
- `OP_SLEEP` は `info.timer.async_wait([this, client_id](...) { ... Next(client_id); });` として raw `this` をキャプチャ
- `OP_EXIT` の case は `Close(callback)` を呼んで `break;` するのみ。switch を抜けて末尾の `Next(client_id)` に到達し、次 op を実行してしまう
- `OP_DISCONNECT` / `OP_RECONNECT` も `Close()` / `Connect()` を呼んで `break;` するのみで、実際の切断・接続完了を待たない
- `Play` は `timer.cancel();` を呼ぶが、既に `boost::asio::post` されている DoNext は cancel できず、次サイクルで二重発火する

## 設計方針

- `ScenarioPlayer` を `std::enable_shared_from_this` にし、`shared_from_this()` / `weak_from_this()` で寿命を保証する
- OP_EXIT の case で `return;` するか、`client_infos_[client_id].paused = true;` を先に立ててから抜ける
- OP_DISCONNECT / OP_RECONNECT は on_close / on_open 相当のコールバック完了で次 op に進む設計に変更する
- `Play` に世代番号 (generation counter) を持たせ、`OnNext` の先頭で世代不一致なら return するようにする
- OpSleep の登録時 (`ScenarioData::Sleep`) と実行時両方で `min <= max` を assert / clamp する

## 完了条件

- `ScenarioPlayer` に raw `this` キャプチャが 0 件になること
- OP_EXIT 後に次 op が実行されないこと (ログで検証)
- OP_DISCONNECT / OP_RECONNECT の後、切断・接続完了を待ってから次 op が実行されること
- `Play` を再度呼んでも二重発火しないこと
- OpSleep の `min > max` を渡しても UB を踏まないこと
