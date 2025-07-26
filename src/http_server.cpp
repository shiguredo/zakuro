#include "http_server.h"

#include <chrono>
#include <iostream>

#include <boost/asio/strand.hpp>
#include <boost/json.hpp>
#include <duckdb.hpp>
#include <rtc_base/logging.h>

#include "zakuro_version.h"

HttpServer::HttpServer(int port) : port_(port) {}

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
    acceptor_.reset(new tcp::acceptor(ioc_, tcp::endpoint(tcp::v4(), port_)));
    DoAccept();
    
    RTC_LOG(LS_INFO) << "HTTP server started on port " << port_;
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
  
  // パスに応じて処理を分岐
  if (req.target() == "/version") {
    return GetVersionResponse(req);
  }

  // 404 Not Found
  http::response<http::string_body> res{http::status::not_found, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "text/plain");
  res.keep_alive(req.keep_alive());
  res.body() = "Not Found";
  res.prepare_payload();
  return res;
}

http::response<http::string_body> HttpSession::GetVersionResponse(
    const http::request<http::string_body>& req) {
  
  boost::json::object json_response;
  
  // Zakuro のバージョン情報
  json_response["zakuro_version"] = ZakuroVersion::GetClientName();
  
  // DuckDB のバージョン情報
  json_response["duckdb_version"] = duckdb::DuckDB::LibraryVersion();
  
  // レスポンスを作成
  http::response<http::string_body> res{http::status::ok, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "application/json");
  res.keep_alive(req.keep_alive());
  res.body() = boost::json::serialize(json_response);
  res.prepare_payload();
  
  return res;
}

// HttpSession の実装

void HttpSession::Run() {
  net::dispatch(stream_.get_executor(),
                beast::bind_front_handler(&HttpSession::DoRead,
                                          shared_from_this()));
}

void HttpSession::DoRead() {
  req_ = {};
  
  stream_.expires_after(std::chrono::seconds(30));
  
  http::async_read(stream_, buffer_, req_,
                   beast::bind_front_handler(&HttpSession::OnRead,
                                             shared_from_this()));
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