# Zakuro::Run の SIGINT/SIGTERM ハンドラが one-shot で 2 回目のシグナルを捕捉できない

- Created: 2026-08-27
- Completed: 2026-09-07
- Branch: feature/fix-signal-handler-one-shot
- Polished: {YYYY-MM-DD}

## 目的

`Zakuro::Run` の `boost::asio::signal_set::async_wait` は one-shot で、
1 回目のシグナルを受けた後は再登録されないため、
2 回目の SIGINT で default disposition (プロセス即終了) にフォールバックし、
VC の後始末や ioc.stop() 待ちが途切れて壊れた状態で終了する経路を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` は以下のように `signal_set` を扱っている。

```cpp
boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
signals.async_wait([&](const boost::system::error_code&, int) { ioc.stop(); });
```

`async_wait` の handler は 1 度呼ばれると再登録されない (boost::asio の signal_set の仕様)。
1 回目の SIGINT で ioc.stop() は呼ばれるが、その後の後始末待ちの間にユーザが Ctrl+C を再度押すと、
2 回目のシグナルは default disposition に処理され、`main` が強制終了する。

現状のコードは「1 回目で穏やかに停止」を意図しているが、2 回目の挙動が明示されていない。
仕様として「2 回目で強制終了」を許容するのか、「2 回目以降も同じ handler で受け続ける」のかを決める必要がある。

## 設計方針

以下いずれか（実装者判断）。

- **再登録する**: `handler` の中で `signals.async_wait(handler);` を再登録する形に変更し、
  2 回目以降のシグナルも一貫して `ioc.stop()` を呼ぶ
- **明示的な 2 段階終了**: 1 回目は穏やかに、2 回目は `std::_exit(1)` などで即終了する形にし、
  仕様としてコメントで明記する

長時間走らせて硬直したときに Ctrl+C 連打で確実に落とせる後者の方が UX として自然だが、
どちらの方針を採るかは Zakuro の運用実態に合わせる。

## 完了条件

- Ctrl+C を 2 回以上押しても、シグナル handler の挙動が予測可能であること (仕様として明記される)
- 2 回目のシグナルで壊れた状態で終了することがないこと (後始末が中途半端にならないか、明示的に強制終了する)

## 解決方法

2026-09-07 追記: 本 issue が報告する「2 回目のシグナルが default disposition にフォールバックして強制終了する」問題は現行実装に存在しないため、コードの修正は行わない。closed とする。

### 根拠 (Boost.Asio 1.91 の実装とソース照合)

- `boost::asio::signal_set` は `async_wait` の再登録有無と OS のシグナルハンドラの有効期間が無関係。
  `signal_set_service::add` がシグナルの登録数が 0 から 1 になったときにハンドラをインストールし、
  インストールされたハンドラは `signal_set` オブジェクトの生存中は動作し続ける。
  ハンドラが `SIG_DFL` に戻るのは `remove` / `clear` で最後の登録が外れたときだけ
  (参照: `boost/asio/detail/impl/signal_set_service.ipp` の `add` / `remove` / `clear`)。
- 待受け中の handler が無い状態でシグナルが届いた場合、`deliver_signal` は
  通知を未配送 (`undelivered_`) としてキューする。
  `boost/asio/basic_signal_set.hpp` のドキュメントにも
  「If a signal is registered with a signal_set, and the signal occurs when there are no waiting
  handlers, then the signal notification is queued.」と明記されており、
  default disposition にフォールバックする経路は存在しない。
- `Zakuro::Run` (`src/zakuro.cpp`) では `signal_set` オブジェクトが
  `ioc.run()` 後の後始末 (`vc->Clear()`) 完了までスコープ内に生存するため、
  後始末待ちの間に 2 回目の SIGINT / SIGTERM が届いても強制終了しない。
  実際の挙動は「1 回目のシグナルで `ioc.stop()`、後続のシグナルは未配送キューに積まれて無視される」である。

### 実測結果

`src/zakuro.cpp` と同一パターン (io_context + signal_set + 再登録無しの async_wait + 1 回目の handler で ioc.stop())
のテストプログラムを Boost 1.91 (DEPS の BOOST_VERSION=1.91.0) でビルド・実行し、以下を確認した。

1. 1 回目の `raise(SIGINT)` で handler が 1 回呼ばれ、`ioc.stop()` が実行される
2. その後にもう一度 `raise(SIGINT)` してもプロセスは強制終了せず、handler も再呼び出しされない (exit code 0)

### 完了条件との関係

- 「Ctrl+C を 2 回以上押してもシグナル handler の挙動が予測可能であること」は、
  「1 回目で `ioc.stop()`、2 回目以降は捕捉され無視される」という現状の挙動で満たされる
- 「2 回目のシグナルで壊れた状態で終了することがないこと」は、
  2 回目のシグナルでプロセスが強制終了する経路自体が存在しないため満たされる
