#include "http_server.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include <boost/asio/strand.hpp>
#include <boost/json.hpp>
// TODO: 将来的にboost::urlを使用する予定
// #include <boost/url.hpp>
#include <duckdb.h>
#include <rtc_base/logging.h>
#include <sora/version.h>
#include <boost/version.hpp>

#include "duckdb_stats_writer.h"
#include "json_rpc.h"
#include "zakuro_version.h"

HttpServer::HttpServer(int port, const std::string& host) : port_(port), host_(host) {}

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

    RTC_LOG(LS_INFO) << "HTTP server started on " << host_ << ":" << port_
                     << " - http://" << host_ << ":" << port_ << "/";
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
    std::make_shared<HttpSession>(std::move(socket), duckdb_writer_,
                                  ui_remote_url_)
        ->Run();
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
  } else {
    // その他のパスはすべてUIへのリバースプロキシ
    return SimpleProxyRequest(req);
  }
}

http::response<http::string_body> HttpSession::SimpleProxyRequest(
    const http::request<http::string_body>& req) {
  try {
    // TODO: 将来的にboost::urlを使用してURLをパースする
    // boost::url url(ui_remote_url_);
    // std::string host = std::string(url.host());
    // std::string scheme = url.scheme();
    // std::string port = url.has_port() ? std::string(url.port()) :
    //                   (scheme == "https" ? "443" : "80");

    // 一時的な簡易URLパース実装
    std::string url_str = ui_remote_url_;
    std::string scheme = "http";
    std::string host;
    std::string port = "80";

    // スキームの判定
    if (url_str.find("https://") == 0) {
      scheme = "https";
      port = "443";
      url_str = url_str.substr(8);
    } else if (url_str.find("http://") == 0) {
      url_str = url_str.substr(7);
    }

    // ホストとポートの分離
    size_t port_pos = url_str.find(':');
    size_t path_pos = url_str.find('/');

    if (port_pos != std::string::npos &&
        (path_pos == std::string::npos || port_pos < path_pos)) {
      host = url_str.substr(0, port_pos);
      if (path_pos != std::string::npos) {
        port = url_str.substr(port_pos + 1, path_pos - port_pos - 1);
      } else {
        port = url_str.substr(port_pos + 1);
      }
    } else if (path_pos != std::string::npos) {
      host = url_str.substr(0, path_pos);
    } else {
      host = url_str;
    }

    // HTTPSの場合はまだサポートしていない
    if (scheme == "https") {
      http::response<http::string_body> res{http::status::not_implemented,
                                            req.version()};
      res.set(http::field::server, "Zakuro");
      res.set(http::field::content_type, "text/plain");
      res.set(http::field::access_control_allow_origin, "*");
      res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
      res.set(http::field::access_control_allow_headers, "Content-Type");
      res.body() = "HTTPS proxy not yet implemented";
      res.keep_alive(req.keep_alive());
      res.prepare_payload();
      return res;
    }

    // 新しいio_contextを作成
    net::io_context ioc;

    // プロキシリクエストを作成
    http::request<http::string_body> proxy_req{req.method(), req.target(),
                                               req.version()};
    proxy_req.set(http::field::host,
                  port == "80" || port == "443" ? host : host + ":" + port);
    proxy_req.set(http::field::user_agent, "Zakuro/1.0");

    // ヘッダーをコピー（Host, Connection, User-Agent以外）
    for (auto const& field : req) {
      std::string field_name = field.name_string();
      // Host, Connection, User-Agentは除外
      if (field_name != "Host" && field_name != "Connection" &&
          field_name != "User-Agent") {
        // 文字列形式でヘッダーを設定（unknownフィールドも正しく処理される）
        proxy_req.set(field_name, field.value());
      }
    }

    // ボディをコピー
    proxy_req.body() = req.body();
    proxy_req.prepare_payload();

    // TCPストリームを作成
    beast::tcp_stream stream(ioc);
    stream.expires_after(std::chrono::seconds(30));

    // 同期的にDNS解決と接続
    tcp::resolver resolver(ioc);
    auto const results = resolver.resolve(host, port);
    stream.connect(results);

    // リクエストを送信
    http::write(stream, proxy_req);

    // レスポンスを受信
    beast::flat_buffer buffer;
    http::response<http::dynamic_body> proxy_res;
    http::read(stream, buffer, proxy_res);

    // ストリームを閉じる
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    std::string body_str = beast::buffers_to_string(proxy_res.body().data());

    // レスポンスを変換
    http::response<http::string_body> res{proxy_res.result(), req.version()};
    res.set(http::field::server, "Zakuro");

    // ヘッダーをコピー
    for (auto const& field : proxy_res) {
      if (field.name() != http::field::connection &&
          field.name() != http::field::transfer_encoding &&
          field.name() != http::field::server) {
        res.set(field.name(), field.value());
      }
    }

    // ボディを変換
    res.body() = body_str;
    res.keep_alive(req.keep_alive());
    res.prepare_payload();

    return res;

  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Proxy error: " << e.what();

    // エラーレスポンス
    http::response<http::string_body> res{http::status::bad_gateway,
                                          req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = "Proxy Error: " + std::string(e.what());
    res.prepare_payload();
    return res;
  }
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

http::response<http::string_body> HttpSession::HandleJsonRpcRequest(
    const http::request<http::string_body>& req) {
  // Content-Typeをチェック
  auto content_type = req[http::field::content_type];
  if (content_type.empty() ||
      content_type.find("application/json") == std::string::npos) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    boost::json::object error_response;
    error_response["jsonrpc"] = "2.0";
    boost::json::object error;
    error["code"] = -32700;
    error["message"] = "Parse error";
    error["data"] = "Content-Type must be application/json";
    error_response["error"] = error;
    error_response["id"] = nullptr;

    res.body() = boost::json::serialize(error_response);
    res.prepare_payload();
    return res;
  }

  // リクエストボディをパース
  boost::system::error_code ec;
  auto jv = boost::json::parse(req.body(), ec);
  if (ec) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    boost::json::object error_response;
    error_response["jsonrpc"] = "2.0";
    boost::json::object error;
    error["code"] = -32700;
    error["message"] = "Parse error";
    error["data"] = ec.message();
    error_response["error"] = error;
    error_response["id"] = nullptr;

    res.body() = boost::json::serialize(error_response);
    res.prepare_payload();
    return res;
  }

  // JsonRpcHandler を使って処理
  JsonRpcHandler handler(duckdb_writer_);
  auto response = handler.Process(jv);

  // レスポンスを作成
  http::response<http::string_body> res{http::status::ok, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "application/json");
  res.keep_alive(req.keep_alive());
  res.body() = boost::json::serialize(response);
  res.prepare_payload();

  return res;
}