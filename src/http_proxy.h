#ifndef HTTP_PROXY_H_
#define HTTP_PROXY_H_

#include <functional>
#include <memory>
#include <string>

// Boost
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

class HttpProxy : public std::enable_shared_from_this<HttpProxy> {
 public:
  HttpProxy(boost::asio::any_io_executor executor,
            const std::string& ui_remote_url);

  void AsyncHandleRequest(
      boost::beast::http::request<boost::beast::http::string_body> req,
      std::function<
          void(boost::beast::http::response<boost::beast::http::string_body>)>
          on_response);

 private:
  void DoResolve();
  void OnResolve(boost::beast::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results);
  void OnConnect(boost::beast::error_code ec);
  void OnSslHandshake(boost::beast::error_code ec);
  void OnWrite(boost::beast::error_code ec, std::size_t bytes_transferred);
  void OnRead(boost::beast::error_code ec, std::size_t bytes_transferred);
  void OnShutdown(boost::beast::error_code ec);

  void FinishResponse();
  void SendErrorResponse(const std::string& message);
  bool ParseUrl(const std::string& url);
  void SetTimeout();

  boost::asio::any_io_executor executor_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> proxy_req_;
  boost::beast::http::response<boost::beast::http::dynamic_body> proxy_res_;
  std::function<void(
      boost::beast::http::response<boost::beast::http::string_body>)>
      on_response_;

  std::unique_ptr<boost::beast::tcp_stream> stream_;
  std::unique_ptr<boost::asio::ssl::context> ssl_ctx_;
  std::unique_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>>
      ssl_stream_;

  std::string ui_remote_url_;
  std::string host_;
  std::string port_;
  std::string host_header_;
  int req_version_;
  bool keep_alive_;
  bool use_ssl_;
};

#endif  // HTTP_PROXY_H_
