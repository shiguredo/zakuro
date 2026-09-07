# Zakuro::Run の SIGINT/SIGTERM ハンドラが one-shot で 2 回目のシグナルを捕捉できない

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
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
