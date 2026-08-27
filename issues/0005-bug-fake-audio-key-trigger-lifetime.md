# FakeAudioKeyTrigger のスレッドが破棄済み io_context / ScenarioPlayer / VirtualClient を触る

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-fake-audio-key-trigger-lifetime
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`FakeAudioKeyTrigger` を保持する `std::unique_ptr<FakeAudioKeyTrigger> trigger;` が
`Zakuro::Run` の関数スコープで宣言されているのに対し、キャプチャした `io_context` / `ScenarioPlayer` / `vcs` は
その内側ブロックで宣言されており、内側ブロック終了後もトリガのバックグラウンドスレッドが 100 ミリ秒後に
破棄済みのオブジェクトへ `boost::asio::post` する use-after-free を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` では以下のスコープ構造になっている。

- 関数スコープで `std::unique_ptr<FakeAudioKeyTrigger> trigger;` を宣言
- 内側ブロック `{ boost::asio::io_context ioc{1}; ... }` の中で `ScenarioPlayer` / `vcs` / `signals` / `timer` を宣言し、
  `trigger.reset(new FakeAudioKeyTrigger(ioc, ..., &scenario_player, vcs));` を呼ぶ
- 内側ブロック終端で `io_context` / `ScenarioPlayer` などが破棄される
- 関数の末尾で `vcs.clear();` を呼び、`trigger` は関数スコープ終端で破棄される

`src/fake_audio_key_trigger.h` の `FakeAudioKeyTrigger` は 100 ミリ秒 sleep + `key_.PopKey()` のループを回し、
入力があると `boost::asio::post(ioc_, [this] { sp_->PauseAll(); })` / `boost::asio::post(ioc_, [this, m] { vcs_[m]->Close(); })`
を実行する。内側ブロック終了後、`trigger` の destructor が呼ばれるまでの間にキー入力が来ると、
破棄済み `io_context` への post、破棄済み `ScenarioPlayer` のポインタ参照、
`vc->Clear()` 済みの `vcs` への操作を実行して UAF する。

## 設計方針

`trigger` の宣言を `io_context` などと同じ内側ブロック内へ移動し、
内側ブロック終了より前に `trigger` の destructor が走ってスレッドが確実に join されるよう順序を担保する。

または、内側ブロックの末尾で `trigger.reset();` を明示的に呼び、
残りのオブジェクトが破棄される前にスレッドを停止させる。

前者（宣言位置の移動）が構造として明快で、修正も小さい。

## 完了条件

- `trigger` が `io_context` より先に destruct されること
- macOS / Linux で `--vcs 3 --duration 10 --repeat-interval 2` を長時間走らせ、
  終了時にキー入力を投げても UAF / セグフォが発生しないこと
- 可能であれば ThreadSanitizer 有効ビルドで race が検知されないこと
