#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

class HttpServer {
 public:
  HttpServer(const std::string& host, int port);
  ~HttpServer();

  // resolve と bind と listen に成功した場合だけ true を返す。
  // 1 回だけ呼ぶ想定で、2 回目以降も false を返す (Stop の後に呼んだ場合は
  // io_context が再開されないため kStartWaitSeconds 待ってから false になる)。
  // resolve の完了も kStartWaitSeconds まで待ち、完了しなければワーカーを
  // 停止して false を返す
  bool Start();
  // 何度呼んでも join は 1 回だけ行う。Start と同時に呼ばないこと
  void Stop();

 private:
  void Run();
  void OnResolve(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results);
  void DoAccept();
  void OnAccept(boost::beast::error_code ec,
                boost::asio::ip::tcp::socket socket);
  // Start の戻り値を確定する。呼び出し元が mutex_ を保持していること。
  // Start が先に待ち時間を超えている場合は何もしない
  void SetStartResult(bool started);
  // 待ち時間を経てから accept を再開する
  void DoAcceptWithRetry();

  // Start が resolve の完了を待つ上限 (秒)
  static constexpr int kStartWaitSeconds = 10;
  // accept 失敗時に再試行するまでの待ち時間 (ミリ秒)
  static constexpr int kAcceptRetryDelayMs = 1000;

  std::string host_;
  int port_;

  boost::asio::io_context ioc_;
  boost::asio::ip::tcp::resolver resolver_;
  std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
  // accept 失敗時にのみ生成する。steady_timer はデフォルトコンストラクタを持たず、
  // ioc_ を渡す必要があるため unique_ptr で遅延生成する
  std::unique_ptr<boost::asio::steady_timer> accept_retry_timer_;

  std::atomic<bool> running_{false};
  // thread_ / start_promise_ / start_result_set_ は Start / Stop / OnResolve から
  // 触るため mutex_ で保護する
  std::mutex mutex_;
  std::unique_ptr<std::thread> thread_;
  std::promise<bool> start_promise_;
  // start_promise_ に値が入ったかどうか。Start が待ち時間を超えた後に set_value を呼ばないために使う
  bool start_result_set_ = false;
};

// HTTP セッションを処理するクラス
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  explicit HttpSession(boost::asio::ip::tcp::socket socket);

  void Run();

 private:
  boost::beast::http::response<boost::beast::http::string_body> HandleRequest(
      boost::beast::http::request<boost::beast::http::string_body> req);
  void SendResponse(
      boost::beast::http::response<boost::beast::http::string_body> res);

  void DoRead();
  void OnRead(boost::beast::error_code ec, std::size_t bytes_transferred);
  void OnWrite(bool keep_alive,
               boost::beast::error_code ec,
               std::size_t bytes_transferred);
  void DoClose();

  // JSON-RPC リクエストを処理する
  boost::beast::http::response<boost::beast::http::string_body>
  HandleJsonRpcRequest(
      const boost::beast::http::request<boost::beast::http::string_body>& req);

  boost::beast::tcp_stream stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>>
      res_;
};

#endif  // HTTP_SERVER_H_
