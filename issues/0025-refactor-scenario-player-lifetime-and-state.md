# ScenarioPlayer のライフタイム管理と op 遷移を修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-scenario-player-lifetime-and-state
- Polished: 2026-09-08

## 目的

`ScenarioPlayer` に以下の問題があり、シナリオが仕様通りに進まない / 破棄後のオブジェクトを触るリスクを孕む。
まとめて修正する。

- `DoNext` の `boost::asio::post`、`OP_SLEEP` の `async_wait`、`OP_EXIT` の `Close` コールバックが raw `this` キャプチャ
- OP_EXIT の case で `return` せず末尾の `Next(client_id)` に落ちて次 op が実行される
- OP_DISCONNECT / OP_RECONNECT を投げっぱなしで完了を待たずに次 op に進む
- `Play(client_id, ...)` を既に動作中の client に対して呼ぶと、既存の DoNext post が残って二重発火する
- OpSleep の `engine_() % (op.max_time_ms - op.min_time_ms + 1)` は `min > max` のとき、
  `max - min + 1` が 0 になれば剰余の第二引数が 0 で UB、0 未満なら [min, max] の範囲外の値になる

## 現状

`src/scenario_player.h` の該当箇所。

- `DoNext` は `boost::asio::post(*config_.ioc, std::bind(&ScenarioPlayer::OnNext, this, client_id));` として raw `this` を bind
- `OP_SLEEP` は `info.timer.async_wait([this, client_id](...) { ... Next(client_id); });` として raw `this` をキャプチャ
- `OP_EXIT` の case は `(*config_.vcs)[client_id]->Close([this, client_id](std::string message) {...});` として
  raw `this` をキャプチャし、`break;` するのみ。switch を抜けて末尾の `Next(client_id)` に到達し、次 op を実行してしまう
- `OP_DISCONNECT` / `OP_RECONNECT` も `Close()` / `Connect()` を呼んで `break;` するのみで、実際の切断・接続完了を待たない
- `Play` は `timer.cancel();` を呼ぶが、既に `boost::asio::post` されている DoNext は cancel できず、次サイクルで二重発火する
- 生成側の `src/zakuro.cpp` は `ScenarioPlayer scenario_player(spc);` としてスタック上に構築しており、
  `enable_shared_from_this` による寿命保証を使うには所有権の変更が必要
- `src/fake_audio_key_trigger.h` は `ScenarioPlayer* sp_;` として ScenarioPlayer を raw ポインタで保持している

## 設計方針

- `ScenarioPlayer` を `std::enable_shared_from_this` にし、コールバック内では `weak_from_this()` を
  lock して有効時のみ実行する (無効なら何もしない) ことで寿命を保証する
- プレイヤー本体は `src/zakuro.cpp` で `std::make_shared<ScenarioPlayer>(spc)` により shared_ptr 管理にする
  (`enable_shared_from_this` は shared_ptr 管理されたオブジェクトでのみ機能する。スタック構築のままだと
  `weak_from_this()` は常に空になり、post / async_wait / Close コールバックが一切発火しない)
- `src/fake_audio_key_trigger.h` の `ScenarioPlayer* sp_` は `std::shared_ptr<ScenarioPlayer>` (または
  `std::weak_ptr<ScenarioPlayer>`) に変更する (raw ポインタのまま残すと寿命保証が意味を持たない)
  - `FakeAudioKeyTrigger` のスレッドと破棄順序の UAF は `issues/0005-bug-fake-audio-key-trigger-lifetime.md` の対象であり、
    本 issue ではポインタの種類の変更のみを行う
  - `sub_scenario_` は既に `std::shared_ptr<ScenarioPlayer>` なのでそのまま
- OP_EXIT の case では `Next(client_id)` に到達しないよう `return;` する
  (`paused = true;` を立てる案は、その後の `ResumeAll` で `OnNext` が OP_EXIT を再実行するため採らない)
- OP_DISCONNECT / OP_RECONNECT は切断・接続完了のコールバックで次 op に進む設計に変更する
  - 切断完了は `VirtualClient::Close(std::function<void(std::string)>)` のコールバック
    (`OnDisconnect` から呼ばれる) を使う
  - 接続完了を通知するコールバックは `VirtualClient` に現存しない。`SoraSignalingObserver::OnSetOffer` を
    接続完了のトリガーとする仕組み (例えば `issues/0024-refactor-virtual-client-state-machine.md` の
    `State::Connected`) を利用するか、`VirtualClient` に接続完了コールバックを追加する。実装前に 0024 との
    関係を確認してから進めること
- `Play` は client 毎の世代番号 (generation counter) を持たせ、再起動時に世代を更新してから op を再開する。
  `DoNext` / `OnNext` は世代番号を捕獲して受け渡し、`OnNext` の先頭で現在の世代と不一致なら return する
  (二重発火の防止)。`OP_SLEEP` の `async_wait` ハンドラも世代番号を捕獲し、不一致なら何もしない
  (発火済みハンドラが `Play` 後に残る経路の防止)
- OpSleep の登録時 (`ScenarioData::Sleep`) と実行時両方で `min <= max` を assert / clamp する

## 完了条件

- `ScenarioPlayer` に raw `this` キャプチャが 0 件になること (post / async_wait / Close コールバックのすべて)
- `ScenarioPlayer` の生成が shared_ptr 管理になり、`FakeAudioKeyTrigger` が ScenarioPlayer の raw ポインタを
  保持しないこと
- OP_EXIT 後に次 op が実行されないこと (ログで検証)
- OP_DISCONNECT / OP_RECONNECT の後、切断・接続完了を待ってから次 op が実行されること
- `Play` を再度呼んでも二重発火しないこと
- OpSleep の `min > max` を渡しても UB を踏まず、待機時間が [min, max] の範囲に収まること
