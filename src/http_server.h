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

  void SetUIRemoteURL(const std::string& url) { ui_remote_url_ = url; }
  std::string GetUIRemoteURL() const { return ui_remote_url_; }

  void Start();
  void Stop();

 private:
  void Run();
  void OnResolve(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results);
  void DoAccept();
  void OnAccept(boost::beast::error_code ec,
                boost::asio::ip::tcp::socket socket);

  std::string host_;
  int port_;
  std::unique_ptr<std::thread> thread_;
  std::atomic<bool> running_{false};
  std::string ui_remote_url_;

  boost::asio::io_context ioc_;
  boost::asio::ip::tcp::resolver resolver_;
  std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
};

// HTTP セッションを処理するクラス
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  explicit HttpSession(boost::asio::ip::tcp::socket socket,
                       const std::string& ui_remote_url);

  void Run();

 private:
  void AsyncHandleRequest(
      boost::beast::http::request<boost::beast::http::string_body> req,
      std::function<
          void(boost::beast::http::response<boost::beast::http::string_body>)>
          on_response);
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

  // リバースプロキシ
  void AsyncHandleSimpleProxyRequest(
      const boost::beast::http::request<boost::beast::http::string_body>& req,
      std::function<
          void(boost::beast::http::response<boost::beast::http::string_body>)>
          on_response);

  boost::beast::tcp_stream stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>>
      res_;
  std::string ui_remote_url_;
};

#endif  // HTTP_SERVER_H_
