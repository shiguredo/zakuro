# VirtualClient::retry_timer_ が io_context より長寿命で UB になる

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-retry-timer-outlives-io-context
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` のスコープ設計により `VirtualClient::retry_timer_` (boost::asio::steady_timer) が
`boost::asio::io_context` より長生きし、`io_context` の破棄後に I/O オブジェクトを破棄することになる
undefined behavior を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` では以下のスコープ構造になっている。

- 内側ブロック `{ boost::asio::io_context ioc{1}; ... }` の中で `ioc` を宣言し、
  `vcs` に対して `VirtualClient::Create(vc_configs[i])` を呼ぶ (`vc_configs[i].sora_config.io_context = &ioc`)
- 内側ブロック終端で `ioc` が破棄される
- 関数の外側スコープで `vcs.clear();` を呼び出す

`VirtualClient` は `src/virtual_client.h` で `boost::asio::steady_timer retry_timer_;` をメンバに持つ。
コンストラクタで `retry_timer_(*config.sora_config.io_context)` により `ioc` へバインドされる。

内側ブロック終端で `ioc` が破棄された後、外側スコープの `vcs.clear();` によって `VirtualClient` が破棄される。

boost::asio の I/O オブジェクトは、`io_context` が所有する service (タイマーなら
`deadline_timer_service`) への参照を内部に保持する。この参照は「owning execution context が存在する限り
有効」とドキュメント化されており、`io_context` の destructor は未実行ハンドラの破棄後に所有する service 群を
`delete` する。したがって `ioc` 破棄後に `retry_timer_` を破棄すると、寿命が尽きた service の member function
(`service_->destroy()`) を呼ぶことになり UB になる。

実際には `Clear()` が先に `retry_timer_.cancel()` を呼ぶため pending waits はなく、
`deadline_timer_service::cancel()` は scheduler の内部データに触れずに早期 return する。
この場合も service の寿命外への member function 呼び出し自体が UB であり、
pending waits が残ったまま破棄した場合は scheduler の内部データにも触れる。

## 設計方針

`vcs.clear();` を内側ブロックの終端直前 (現状の `for (auto& vc : vcs) { vc->Clear(); }` の直後) に移動する。
1 行の移動で `VirtualClient` (と `retry_timer_`) が `io_context` より先に破棄されるようになる。

`vcs` 自体は `std::vector<std::shared_ptr<VirtualClient>>` として外側スコープに残しても構わない
(clear 後は空の vector なので UB の原因にはならない)。

## 完了条件

- `vcs.clear();` が `ioc` の破棄より前に実行されること
- `VirtualClient` (と `retry_timer_`) が `io_context` より先に destruct され、service が生存しているうちに破棄されること
- 通常のビルド (例: `python run.py build <target>`) が成功すること
- サニタイザを有効にできる環境があれば、`boost::asio::steady_timer` に関する UB / UAF が検知されないこと
