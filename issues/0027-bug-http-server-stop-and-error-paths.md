# HttpServer の Stop 非スレッドセーフと Resolve/Accept エラーパスを修正する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-http-server-stop-and-error-paths
- Polished: {YYYY-MM-DD}

## 目的

`HttpServer` に以下 3 種の問題があり、正式リリースまでに解消する。

- `Stop` が非スレッドセーフで `thread_->join()` を二重に呼ぶ可能性がある
- `Start` が bind 成否を呼び出し元に返さず、失敗しても「HTTP サーバー起動」と嘘のログが出て気付けない
- `OnAccept` の永続エラー (fd 枯渇等) で `DoAccept()` を即再開して無限ループする

## 現状

### Stop の非スレッドセーフ

`src/http_server.cpp` の `HttpServer::Stop` は `if (!running_) return;` の後
`running_ = false; ioc_.stop(); thread_->join(); thread_ = nullptr;` を行う。
`running_` は atomic だが、複数スレッドから同時に呼ばれると `join` が二重呼び出しになる可能性がある。
destructor から Stop が呼ばれる経路も含めて、単一呼び出しを保証する必要がある。

### Start の bind 失敗を呼び出し元に返さない

`HttpServer::Start` は void 返り値で、`running_ = true; thread_.reset(new std::thread([this] { Run(); }));` するだけ。
`OnResolve` の中で `acceptor(ioc_, endpoint)` が bind 失敗の例外を投げると、
`Run()` の catch でログを出して ioc.run() から戻るのみ。
`main.cpp` は `Start` の後 `RTC_LOG(LS_INFO) << "HTTP server started ...";` を出してそのまま次の処理に進み、
ユーザは HTTP RPC を叩けない理由が分からない。

### OnAccept の永続エラー無限ループ

`HttpServer::OnAccept` はエラー時に `RTC_LOG(LS_ERROR) << "Accept error: " << ec.message();` してから
`if (running_) DoAccept();` を無条件で呼ぶ。
`EMFILE` (ファイルディスクリプタ枯渇) などの永続エラーでは、同じエラーで即座に再度失敗し
CPU 100% のスピンループになる。

## 設計方針

### Stop の排他制御

`std::atomic<bool>` の `exchange` で「一度だけ Stop する」実装にする。
`if (running_.exchange(false))` の分岐内でのみ ioc.stop / join / reset を実行する。

### Start の bind 成否伝搬

`Start` を `bool` 返り値にする。または `std::promise<bool>` で `OnResolve` の完了 (bind 成功) を待ってから返す。
`main.cpp` 側では失敗時に `RTC_LOG(LS_ERROR)` を出して `return 1;` する。

### OnAccept のバックオフ

永続エラー時にバックオフを入れる (例: 1 秒後に再試行、あるいは失敗回数で判定してサーバー停止に切り替える)。
`EMFILE` は fd 枯渇なので、根本原因は他所にあるが、少なくともスピンループは避ける。

## 完了条件

- `HttpServer::Stop` を複数スレッドから同時に呼んでも join が 1 度だけ実行されること
- bind に失敗した場合、`main` は非ゼロ終了し明確なエラーメッセージを出すこと
- `EMFILE` シミュレーションで CPU 100% のスピンループにならないこと
