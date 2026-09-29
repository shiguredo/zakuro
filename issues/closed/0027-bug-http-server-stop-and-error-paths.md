# HttpServer の Stop 非スレッドセーフと Resolve/Accept エラーパスを修正する

- Created: 2026-08-27
- Completed: 2026-09-30
- Branch: feature/fix-http-server-stop-and-error-paths
- Polished: 2026-09-07
- Milestone: 2026.1.0

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
`OnResolve` 内で catch して設定) と `Stop` で必ず設定する。

resolve が返らない場合に `Start` が永久にブロックしないよう、待ち時間の上限を設ける。
上限を超えた場合はワーカースレッドを停止してから false を返す。停止しないまま呼び出し元が
`HttpServer` を破棄すると、ワーカーが破棄済みの `io_context` の中で動き続けるためである。

`main.cpp` 側では false の場合にエラーメッセージを出して `return 1;` する。
メッセージは `std::cerr` に出す (`--http-host` と `--http-port` の片方だけを指定した
場合のメッセージと同じ出力先に揃えるため)。

### OnAccept のバックオフ

accept エラー時は即時に `DoAccept()` を呼び直さない。停止処理による
`boost::asio::error::operation_aborted` は再試行せず、それ以外のエラーはタイマーで一定時間
(1 秒) 待ってから再試行する。再試行を待つ場合はその旨を WARNING でログに出す。

`EMFILE` は fd 枯渇であり、接続の終了に伴い解消し得る一時的なエラーなので、
バックオフで再試行し続ける方針とし、サーバー停止への切替は行わない。
根本原因は他所にあるが、少なくともスピンループは避ける。

## 完了条件

- `HttpServer::Stop` を複数スレッドから同時に呼んでも join が 1 度だけ実行されること
- bind または resolve に失敗した場合、`main` は非ゼロ終了し明確なエラーメッセージを出すこと
- `EMFILE` シミュレーション (fd を枯渇させて accept に失敗させる) で CPU 100% のスピンループにならないこと

`Stop` の同時呼び出しは、実バイナリでは `Stop` がプロセス内で 1 回しか呼ばれないため
E2E で再現できない。`HttpServer` を直接呼ぶ一時的な検証プログラムで `Stop` を 4 スレッドから
並行に呼び、20 ラウンドとも異常終了しないことを確認した。

正常終了のパスも `test/test_http_server.py` では検証しない。SIGTERM を送ったときの終了コードは
実行環境によって安定せず (テスト環境では SIGTERM の既定動作で終わる場合と、ハンドラを経由して
0 で終わる場合がある)、join 漏れの退行を決定的に検出できないためである。代わりに実バイナリへ
`kill -TERM` を送る確認を手動で行い、join を外したビルドでは SIGABRT (終了コード 134)、
join を入れたビルドでは 3 回とも終了コード 0 になることを確認した。

残る 2 項目は `test/test_http_server.py` が実バイナリを起動して検証する。

## 解決方法

`src/http_server.cpp` と `src/http_server.h` を次のように修正した。

- `Start` を `bool` 返り値にし、`std::promise<bool>` で `OnResolve` の完了を待つ。`Start` は
  resolve と bind に成功して accept を開始できた場合だけ true を返す
- promise と `start_result_set_` は `std::mutex` で保護し、`thread_` も同じ mutex で保護する。
  `Stop` は mutex の下で `thread_` を取り出し、non-null のときだけ join する
  (join は 1 回だけ行う)
- `Stop` は `running_.exchange(false)` が true を返したときだけ本体を実行する
- resolve の完了を `kStartWaitSeconds` (10 秒) まで待ち、超えた場合はワーカースレッドを
  停止してから false を返す。停止しないまま呼び出し元が破棄すると、ワーカーが破棄済みの
  `io_context` の中で動き続けるためである
- `OnAccept` は `operation_aborted` では再試行せず、それ以外のエラーでは 1 秒待ってから
  再試行する。待つ場合は `Accept failed, retrying after <ミリ秒> ms` を WARNING で出す
- accept エラーのログを ERROR から WARNING に変更した

`Start` は 1 回だけ呼ぶ契約とし、`Start` と同時に `Stop` を呼ばないこととした。
`io_context` を `restart()` しないため、`Stop` の後に `Start` を呼んでも起動しない
(待ち時間の上限まで待って false を返す)。この契約は `src/http_server.h` のコメントに
明記している。

`src/main.cpp` は `Start` が false の場合に `failed to start HTTP server on <host>:<port>` を
`std::cerr` に出して `return 1;` する。`RTC_LOG(LS_ERROR)` ではなく `std::cerr` にしたのは、
`--http-host` と `--http-port` の片方だけを指定した場合のメッセージと同じ出力先に
揃えるためである。

`test/test_http_server.py` を追加した。

- bind 失敗 (ポート使用中 / ローカルに割り当てられていないアドレス) と resolve 失敗の 3 件で、
  終了コード 1、失敗理由、`failed to start HTTP server on` を確認し、起動済みのログが
  出ないことを確認する
- fd 枯渇で accept が失敗し続ける状況を作り、3 秒間の accept エラーの件数が待ち時間から
  決まる件数以下であることと、再試行が 2 件以上出ることを確認する。あわせて失敗の理由が
  `Too many open files` (EMFILE) であることを確認する。macOS arm64 での実測は 3 秒間で
  4 件、再試行も 4 件だった。ファイルディスクリプタの上限を下げると libwebrtc のスレッドも
  ファイルディスクリプタを作れなくなり、Linux では accept とは無関係な SIGABRT で終了する。
  そのため、このテストでは終了コードを検証しない
- `Stop` の同時呼び出しは上記のとおり E2E で再現できないため対象外とし、モジュールの
  docstring に明記する

`CHANGES.md` の `## develop` に `[FIX]` のエントリを 2 件追加した。
