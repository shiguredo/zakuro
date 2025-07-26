#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <atomic>
#include <memory>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer {
 public:
  HttpServer(int port);
  ~HttpServer();

  void Start();
  void Stop();

 private:
  void Run();
  void DoAccept();
  void OnAccept(beast::error_code ec, tcp::socket socket);

  int port_;
  std::unique_ptr<std::thread> thread_;
  std::atomic<bool> running_{false};
  
  net::io_context ioc_;
  std::unique_ptr<tcp::acceptor> acceptor_;
};

// HTTP セッションを処理するクラス
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  explicit HttpSession(tcp::socket&& socket) : stream_(std::move(socket)) {}

  void Run();
  
  static http::response<http::string_body> HandleRequest(
      http::request<http::string_body>&& req);
  static http::response<http::string_body> GetVersionResponse(
      const http::request<http::string_body>& req);

 private:
  void DoRead();
  void OnRead(beast::error_code ec, std::size_t bytes_transferred);
  void SendResponse(http::response<http::string_body>&& res);
  void OnWrite(bool keep_alive,
               beast::error_code ec,
               std::size_t bytes_transferred);
  void DoClose();

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  std::shared_ptr<http::response<http::string_body>> res_;
};

#endif  // HTTP_SERVER_H_