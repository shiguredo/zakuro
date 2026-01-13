#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <atomic>
#include <memory>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

class HttpServer {
 public:
  HttpServer(const std::string& host, int port);
  ~HttpServer();

  void Start();
  void Stop();

  boost::asio::io_context& GetIOContext() { return ioc_; }

 private:
  void Run();
  void DoAccept();
  void OnAccept(boost::beast::error_code ec,
                boost::asio::ip::tcp::socket socket);

  int port_;
  std::string host_;
  std::unique_ptr<std::thread> thread_;
  std::atomic<bool> running_{false};

  boost::asio::io_context ioc_;
  std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
};

// HTTP セッションを処理するクラス
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  explicit HttpSession(boost::asio::ip::tcp::socket&& socket)
      : stream_(std::move(socket)) {}

  void Run();

  boost::beast::http::response<boost::beast::http::string_body> HandleRequest(
      boost::beast::http::request<boost::beast::http::string_body>&& req);

 private:
  void DoRead();
  void OnRead(boost::beast::error_code ec, std::size_t bytes_transferred);
  void SendResponse(
      boost::beast::http::response<boost::beast::http::string_body>&& res);
  void OnWrite(bool keep_alive,
               boost::beast::error_code ec,
               std::size_t bytes_transferred);
  void DoClose();

  boost::beast::tcp_stream stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>>
      res_;
};

#endif  // HTTP_SERVER_H_
