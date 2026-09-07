# HttpServer の Stop 非スレッドセーフと Resolve/Accept エラーパスを修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-http-server-stop-and-error-paths
- Polished: 2026-09-07

## 目的

`HttpServer` に以下 3 種の問題があり、正式リリースまでに解消する。

- `Stop` が非スレッドセーフで `thread_->join()` を二重に呼ぶ可能性がある
- `Start` が bind / resolve の失敗を呼び出し元に返さず、失敗しても「HTTP サーバー起動」と嘘のログが出て気付けない
- `OnAccept` の永続エラー (fd 枯渇等) で `DoAccept()` を即再開して無限ループする

## 現状

### Stop の非スレッドセーフ

`src/http_server.cpp` の `HttpServer::Stop` は `if (!running_) return;` の後
`running_ = false; ioc_.stop(); thread_->join(); thread_ = nullptr;` を行う。
`running_` は atomic だが、複数スレッドから同時に呼ばれると `join` が二重呼び出しになる可能性がある。
destructor から Stop が呼ばれる経路も含めて、単一呼び出しを保証する必要がある。

### Start の bind / resolve 失敗を呼び出し元に返さない

`HttpServer::Start` は void 返り値で、`running_ = true; thread_.reset(new std::thread([this] { Run(); }));` するだけ。
`OnResolve` の中で `acceptor(ioc_, endpoint)` が bind 失敗の例外を投げると、
`Run()` の catch でログを出して ioc.run() から戻るのみ。
同様に `OnResolve` の resolve エラーや結果空でもログを出して戻るだけで、
スレッド起動後の失敗は `Start` の呼び出し元に伝わらない。
`main.cpp` は `Start` の後 `RTC_LOG(LS_INFO) << "HTTP server started ...";` を出してそのまま次の処理に進み、
ユーザーは HTTP RPC を叩けない理由が分からない。

### OnAccept の永続エラー無限ループ

`HttpServer::OnAccept` はエラー時に `RTC_LOG(LS_ERROR) << "Accept error: " << ec.message();` してから
`if (running_) DoAccept();` を無条件で呼ぶ。
`EMFILE` (ファイルディスクリプタ枯渇) などの永続エラーでは、同じエラーで即座に再度失敗し
CPU 100% のスピンループになる。

## 設計方針

### Stop の排他制御

`std::atomic<bool>` の `exchange` で「一度だけ Stop する」実装にする。
`if (running_.exchange(false))` の分岐内でのみ ioc.stop / join / reset を実行する。

### Start の bind / resolve 成否伝搬

`Start` を `bool` 返り値にする。既存の非同期構造 (`async_resolve` → `OnResolve`) を保ったまま、
`std::promise<bool>` で `OnResolve` の完了 (accept 開始可否) を待ってから返す。
promise は `OnResolve` の全終了経路 (bind 成功・resolve エラー・結果空・bind 例外は
`OnResolve` 内で catch して設定) で必ず設定し、`Start` が永久ブロックしないようにする。

`main.cpp` 側では false の場合に `RTC_LOG(LS_ERROR)` を出して `return 1;` する。

### OnAccept のバックオフ

accept エラー時は即時に `DoAccept()` を呼び直さない。停止処理による
`boost::asio::error::operation_aborted` は再試行せず、それ以外のエラーはタイマーで一定時間
(例: 1 秒) 待ってから再試行する。

`EMFILE` は fd 枯渇であり、接続の終了に伴い解消し得る一時的なエラーなので、
バックオフで再試行し続ける方針とし、サーバー停止への切替は行わない。
根本原因は他所にあるが、少なくともスピンループは避ける。

## 完了条件

- `HttpServer::Stop` を複数スレッドから同時に呼んでも join が 1 度だけ実行されること
- bind または resolve に失敗した場合、`main` は非ゼロ終了し明確なエラーメッセージを出すこと
- `EMFILE` シミュレーション (fd を枯渇させて accept に失敗させる) で CPU 100% のスピンループにならないこと
