#include "http_server.h"

#include <chrono>

#include <rtc_base/logging.h>
#include <boost/asio/strand.hpp>
#include <boost/json.hpp>

#include "json_rpc.h"

HttpServer::HttpServer(int port, const std::string& host)
    : port_(port), host_(host) {}

HttpServer::~HttpServer() {
  Stop();
}

void HttpServer::Start() {
  if (running_) {
    return;
  }

  running_ = true;
  thread_.reset(new std::thread([this]() { Run(); }));
}

void HttpServer::Stop() {
  if (!running_) {
    return;
  }

  running_ = false;
  ioc_.stop();

  if (thread_ && thread_->joinable()) {
    thread_->join();
  }
}

void HttpServer::Run() {
  try {
    auto const address = net::ip::make_address(host_);
    acceptor_.reset(new tcp::acceptor(ioc_, tcp::endpoint(address, port_)));
    DoAccept();

    ioc_.run();
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "HTTP server error: " << e.what();
  }
}

void HttpServer::DoAccept() {
  acceptor_->async_accept(
      net::strand<net::io_context::executor_type>(ioc_.get_executor()),
      beast::bind_front_handler(&HttpServer::OnAccept, this));
}

void HttpServer::OnAccept(beast::error_code ec, tcp::socket socket) {
  if (ec) {
    RTC_LOG(LS_ERROR) << "Accept error: " << ec.message();
  } else {
    std::make_shared<HttpSession>(std::move(socket))->Run();
  }

  if (running_) {
    DoAccept();
  }
}

http::response<http::string_body> HttpSession::HandleRequest(
    http::request<http::string_body>&& req) {
  // JSON-RPC エンドポイント
  if (req.target() == "/rpc" && req.method() == http::verb::post) {
    return HandleJsonRpcRequest(req);
  }

  // その他のリクエストには 404 Not Found を返す
  http::response<http::string_body> res{http::status::not_found, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "text/plain");
  res.keep_alive(req.keep_alive());
  res.body() = "Not Found";
  res.prepare_payload();
  return res;
}

http::response<http::string_body> HttpSession::HandleJsonRpcRequest(
    const http::request<http::string_body>& req) {
  http::response<http::string_body> res{http::status::ok, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "application/json");
  res.keep_alive(req.keep_alive());

  try {
    // リクエストボディをパース
    boost::system::error_code ec;
    auto json_request = boost::json::parse(req.body(), ec);
    if (ec) {
      // パースエラー
      boost::json::object error_response;
      error_response["jsonrpc"] = "2.0";
      error_response["id"] = nullptr;
      boost::json::object error;
      error["code"] = -32700;
      error["message"] = "Parse error";
      error["data"] = ec.message();
      error_response["error"] = error;
      res.body() = boost::json::serialize(error_response);
      res.prepare_payload();
      return res;
    }

    // JSON-RPC ハンドラーで処理
    JsonRpcHandler handler;
    auto response = handler.Process(json_request);
    res.body() = boost::json::serialize(response);
  } catch (const std::exception& e) {
    // 内部エラー
    boost::json::object error_response;
    error_response["jsonrpc"] = "2.0";
    error_response["id"] = nullptr;
    boost::json::object error;
    error["code"] = -32603;
    error["message"] = "Internal error";
    error["data"] = e.what();
    error_response["error"] = error;
    res.body() = boost::json::serialize(error_response);
  }

  res.prepare_payload();
  return res;
}

// HttpSession の実装

void HttpSession::Run() {
  net::dispatch(
      stream_.get_executor(),
      beast::bind_front_handler(&HttpSession::DoRead, shared_from_this()));
}

void HttpSession::DoRead() {
  req_ = {};

  stream_.expires_after(std::chrono::seconds(30));

  http::async_read(
      stream_, buffer_, req_,
      beast::bind_front_handler(&HttpSession::OnRead, shared_from_this()));
}

void HttpSession::OnRead(beast::error_code ec, std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec == http::error::end_of_stream) {
    return DoClose();
  }

  if (ec) {
    RTC_LOG(LS_ERROR) << "Read error: " << ec.message();
    return;
  }

  // リクエストを処理
  SendResponse(HttpSession::HandleRequest(std::move(req_)));
}

void HttpSession::SendResponse(http::response<http::string_body>&& res) {
  res_ = std::make_shared<http::response<http::string_body>>(std::move(res));

  http::async_write(
      stream_, *res_,
      beast::bind_front_handler(&HttpSession::OnWrite, shared_from_this(),
                                res_->keep_alive()));
}

void HttpSession::OnWrite(bool keep_alive,
                          beast::error_code ec,
                          std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    RTC_LOG(LS_ERROR) << "Write error: " << ec.message();
    return;
  }

  if (!keep_alive) {
    return DoClose();
  }

  res_.reset();
  DoRead();
}

void HttpSession::DoClose() {
  beast::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
}
