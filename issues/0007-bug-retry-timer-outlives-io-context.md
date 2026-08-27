# VirtualClient::retry_timer_ が io_context より長寿命で UB になる

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-retry-timer-outlives-io-context
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` のスコープ設計により `VirtualClient::retry_timer_` (boost::asio::steady_timer) が
`boost::asio::io_context` より長生きし、boost::asio の I/O オブジェクトのライフタイム契約に違反する
undefined behavior を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` では以下のスコープ構造になっている。

- 内側ブロック `{ boost::asio::io_context ioc{1}; ... }` の中で `ioc` を宣言し、
  `vcs` に対して `VirtualClient::Create(vc_configs[i])` を呼ぶ (`vc_configs[i].sora_config.io_context = &ioc`)
- 内側ブロック終端で `ioc` が破棄される
- 関数の外側スコープで `vcs.clear();` を呼び出す

`VirtualClient` は `src/virtual_client.h` で `boost::asio::steady_timer retry_timer_;` をメンバに持つ。
コンストラクタで `retry_timer_(*config.sora_config.io_context)` により `ioc` へバインドされる。

内側ブロック終端で `ioc` が破棄された後、外側スコープの `vcs.clear();` によって `VirtualClient` が破棄される時、
`retry_timer_` の destructor が既に破棄された `io_context` の内部データを触る。
boost::asio の contract は「I/O オブジェクトは io_context より先に破棄しなければならない」と規定しており、
この順序違反は UB になる。

## 設計方針

`vcs.clear();` を内側ブロックの終端直前 (現状の `for (auto& vc : vcs) { vc->Clear(); }` の直後) に移動する。
1 行の移動で `VirtualClient` (と `retry_timer_`) が `io_context` より先に破棄されるようになる。

`vcs` 自体は `std::vector<std::shared_ptr<VirtualClient>>` として外側スコープに残しても構わない
(clear 後は空の vector なので UB の原因にはならない)。

## 完了条件

- `vcs.clear();` が `ioc` の破棄より前に実行されること
- boost::asio のドキュメントに従い、`VirtualClient` (と `retry_timer_`) が `io_context` より先に destruct されること
- サニタイザ有効ビルドで、`boost::asio::steady_timer` に関する UB / UAF が検知されないこと
