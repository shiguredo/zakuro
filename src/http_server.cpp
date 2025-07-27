#include "http_server.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include <boost/asio/strand.hpp>
#include <boost/json.hpp>
#include <boost/url.hpp>
#include <boost/version.hpp>
#include <duckdb.hpp>
#include <rtc_base/logging.h>
#include <sora/version.h>

#include "duckdb_stats_writer.h"
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
    std::make_shared<HttpSession>(std::move(socket), duckdb_writer_, ui_remote_url_)->Run();
  }

  if (running_) {
    DoAccept();
  }
}

http::response<http::string_body> HttpSession::HandleRequest(
    http::request<http::string_body>&& req) {
  
  // OPTIONSメソッドの処理（CORS preflight）
  if (req.method() == http::verb::options) {
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.set(http::field::access_control_max_age, "3600");
    res.keep_alive(req.keep_alive());
    res.prepare_payload();
    return res;
  }
  
  if (req.target() == "/version") {
    return GetVersionResponse(req);
  } else if (req.target() == "/query" && req.method() == http::verb::post) {
    return GetQueryResponse(req);
  } else {
    // その他のパスはすべてUIへのリバースプロキシ
    return SimpleProxyRequest(req);
  }
}

http::response<http::string_body> HttpSession::GetVersionResponse(
    const http::request<http::string_body>& req) {
  
  boost::json::object json_response;
  
  // Zakuro のバージョン情報
  json_response["zakuro"] = ZakuroVersion::GetVersion();
  
  // DuckDB のバージョン情報
  json_response["duckdb"] = duckdb::DuckDB::LibraryVersion();
  
  // Sora C++ SDK のバージョン情報
  json_response["sora_cpp_sdk"] = ZakuroVersion::GetSoraCppSdkVersion();
  
  // libwebrtc のバージョン情報
  json_response["libwebrtc"] = ZakuroVersion::GetWebRTCVersion();
  
  // Boost のバージョン情報
  // BOOST_VERSION は 108800 のような整数値で、これは 1.88.0 を表す：
  // - 1 = BOOST_VERSION / 100000 (メジャーバージョン)
  // - 88 = (BOOST_VERSION / 100) % 1000 (マイナーバージョン)
  // - 0 = BOOST_VERSION % 100 (パッチバージョン)
  json_response["boost"] = std::to_string(BOOST_VERSION / 100000) + "." +
                          std::to_string((BOOST_VERSION / 100) % 1000) + "." +
                          std::to_string(BOOST_VERSION % 100);
  
  // レスポンスを作成
  http::response<http::string_body> res{http::status::ok, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, ui_remote_url_);
  res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
  res.set(http::field::access_control_allow_headers, "Content-Type");
  res.keep_alive(req.keep_alive());
  res.body() = boost::json::serialize(json_response);
  res.prepare_payload();
  
  return res;
}

http::response<http::string_body> HttpSession::GetQueryResponse(
    const http::request<http::string_body>& req) {
  
  // DuckDBWriterが設定されていない場合
  if (!duckdb_writer_) {
    http::response<http::string_body> res{http::status::service_unavailable, req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.keep_alive(req.keep_alive());
    
    boost::json::object error;
    error["error"] = "Database not available";
    res.body() = boost::json::serialize(error);
    res.prepare_payload();
    return res;
  }
  
  // リクエストボディからSQL文を取得
  std::string sql = req.body();
  
  // Content-Typeをチェック
  auto content_type = req[http::field::content_type];
  if (!content_type.empty() && content_type != "application/sql") {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.keep_alive(req.keep_alive());
    
    boost::json::object error;
    error["error"] = "Content-Type must be application/sql";
    res.body() = boost::json::serialize(error);
    res.prepare_payload();
    return res;
  }
  
  // SQL文が空の場合
  if (sql.empty()) {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.keep_alive(req.keep_alive());
    
    boost::json::object error;
    error["error"] = "SQL query is required";
    res.body() = boost::json::serialize(error);
    res.prepare_payload();
    return res;
  }
  
  // クエリを実行
  std::string result_json = duckdb_writer_->ExecuteQuery(sql);
  
  // レスポンスを作成
  http::response<http::string_body> res{http::status::ok, req.version()};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, ui_remote_url_);
  res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
  res.set(http::field::access_control_allow_headers, "Content-Type");
  res.keep_alive(req.keep_alive());
  res.body() = result_json;
  res.prepare_payload();
  
  return res;
}

http::response<http::string_body> HttpSession::SimpleProxyRequest(
    const http::request<http::string_body>& req) {
  try {
    // URLをパース
    boost::url url(ui_remote_url_);
    std::string host = std::string(url.host());
    std::string scheme = url.scheme();
    std::string port = url.has_port() ? std::string(url.port()) : 
                      (scheme == "https" ? "443" : "80");
    
    // HTTPSの場合はまだサポートしていない
    if (scheme == "https") {
      http::response<http::string_body> res{http::status::not_implemented, req.version()};
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
    http::request<http::string_body> proxy_req{req.method(), req.target(), req.version()};
    proxy_req.set(http::field::host, port == "80" || port == "443" ? host : host + ":" + port);
    proxy_req.set(http::field::user_agent, "Zakuro/1.0");
    
    // ヘッダーをコピー（Host, Connection, User-Agent以外）
    for (auto const& field : req) {
      std::string field_name = field.name_string();
      // Host, Connection, User-Agentは除外
      if (field_name != "Host" && 
          field_name != "Connection" &&
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
    
    // CORSヘッダーを追加
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    
    // ボディを変換
    res.body() = body_str;
    res.keep_alive(req.keep_alive());
    res.prepare_payload();
    
    return res;
    
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "Proxy error: " << e.what();
    
    // エラーレスポンス
    http::response<http::string_body> res{http::status::bad_gateway, req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "text/plain");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.keep_alive(req.keep_alive());
    res.body() = "Proxy Error: " + std::string(e.what());
    res.prepare_payload();
    return res;
  }
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