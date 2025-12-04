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

class DuckDBStatsWriter;

class HttpServer {
 public:
  HttpServer(int port, const std::string& host = "127.0.0.1");
  ~HttpServer();

  void SetUIRemoteURL(const std::string& url) { ui_remote_url_ = url; }
  std::string GetUIRemoteURL() const { return ui_remote_url_; }

  void SetDuckDBWriter(std::shared_ptr<DuckDBStatsWriter> writer) {
    duckdb_writer_ = writer;
  }

  void Start();
  void Stop();

  net::io_context& GetIOContext() { return ioc_; }

 private:
  void Run();
  void DoAccept();
  void OnAccept(beast::error_code ec, tcp::socket socket);

  int port_;
  std::string host_;
  std::unique_ptr<std::thread> thread_;
  std::atomic<bool> running_{false};
  std::string ui_remote_url_;
  std::shared_ptr<DuckDBStatsWriter> duckdb_writer_;

  net::io_context ioc_;
  std::unique_ptr<tcp::acceptor> acceptor_;
};

// HTTP セッションを処理するクラス
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession(tcp::socket&& socket,
              const std::string& ui_remote_url,
              std::shared_ptr<DuckDBStatsWriter> duckdb_writer)
      : stream_(std::move(socket)),
        ui_remote_url_(ui_remote_url),
        duckdb_writer_(duckdb_writer) {}

  void Run();

  http::response<http::string_body> HandleRequest(
      http::request<http::string_body>&& req);

 private:
  void DoRead();
  void OnRead(beast::error_code ec, std::size_t bytes_transferred);
  void SendResponse(http::response<http::string_body>&& res);
  void OnWrite(bool keep_alive,
               beast::error_code ec,
               std::size_t bytes_transferred);
  void DoClose();

  // JSON-RPC リクエストを処理する
  http::response<http::string_body> HandleJsonRpcRequest(
      const http::request<http::string_body>& req);

  // リバースプロキシ
  http::response<http::string_body> SimpleProxyRequest(
      const http::request<http::string_body>& req);

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  std::shared_ptr<http::response<http::string_body>> res_;
  std::string ui_remote_url_;
  std::shared_ptr<DuckDBStatsWriter> duckdb_writer_;
};

#endif  // HTTP_SERVER_H_
