# FakeAudioKeyTrigger のスレッドが破棄済み io_context / ScenarioPlayer / VirtualClient を触る

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-fake-audio-key-trigger-lifetime
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`FakeAudioKeyTrigger` を保持する `std::unique_ptr<FakeAudioKeyTrigger> trigger;` が
`Zakuro::Run` の関数スコープで宣言されているのに対し、キャプチャした `io_context` / `ScenarioPlayer` は
その内側ブロックで宣言され、`vcs` は関数スコープで宣言されて関数末尾で `vcs.clear()` される。
内側ブロック終了後もトリガのバックグラウンドスレッドが 100 ミリ秒後に破棄済みのオブジェクトへ
`boost::asio::post` する use-after-free を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` では以下のスコープ構造になっている。

- 関数スコープで `std::unique_ptr<FakeAudioKeyTrigger> trigger;` を宣言
- 関数スコープで `std::vector<std::shared_ptr<VirtualClient>> vcs;` を宣言
- 内側ブロック `{ boost::asio::io_context ioc{1}; ... }` の中で `ScenarioPlayer` / `signals` / `timer` を宣言し、
  `trigger.reset(new FakeAudioKeyTrigger(ioc, ..., &scenario_player, vcs));` を呼ぶ
- 内側ブロック終端で `io_context` / `ScenarioPlayer` などが破棄される（`vcs` のベクタ自体は関数スコープに残る）
- 関数の末尾で `vcs.clear();` を呼び、`trigger` は関数スコープ終端で破棄される
- `vcs` は `trigger` より後で宣言されているため、関数終端では `vcs` の destructor が
  `trigger` の destructor より先に実行される

`src/fake_audio_key_trigger.h` の `FakeAudioKeyTrigger` は 100 ミリ秒 sleep + `key_.PopKey()` のループを回し、
入力があると `boost::asio::post(ioc_, [this] { sp_->PauseAll(); })` / `boost::asio::post(ioc_, [this, m] { vcs_[m]->Close(); })`
を実行する。内側ブロック終了後から `trigger` の destructor が呼ばれるまでの間にキー入力が来ると、
バックグラウンドスレッドが破棄済みの `io_context` に対して `boost::asio::post` を実行して UAF になる。
(post されたハンドラは `ioc.run()` 終了後は実行されないが、`sp_` / `vcs_` への参照を含む)
さらに、関数終端では `vcs` の destructor が `trigger` の destructor より先に実行されるため、
join 待ちの間にスレッドが `vcs_.size()` へアクセスして破棄済みのベクタを参照し得る。

## 設計方針

`trigger` の宣言を、内側ブロック内で `scenario_player` の生成後の位置（現在 `trigger.reset(...)` を呼んでいる位置の直前）
へ移動し、内側ブロック終了より前に `trigger` の destructor が走ってスレッドが確実に join されるよう順序を担保する。
(`io_context` / `ScenarioPlayer` の destructor が `trigger` の destructor より先に実行されない宣言順序にする)

または、内側ブロックの末尾で `trigger.reset();` を明示的に呼び、
残りのオブジェクトが破棄される前にスレッドを停止させる。

前者（宣言位置の移動）が構造として明快で、修正も小さい。

## 完了条件

- `trigger` の destructor（バックグラウンドスレッドの join）が `io_context` / `ScenarioPlayer` の destructor より
  先に実行されること
- macOS / Linux で `--vcs 3 --duration 10 --repeat-interval 2` を長時間走らせ、
  終了時にキー入力を投げても UAF / セグフォが発生しないこと
- 可能であれば ThreadSanitizer 有効ビルドで race が検知されないこと
