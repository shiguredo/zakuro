#include "http_server.h"

#include <chrono>

#include <openssl/ssl.h>
#include <rtc_base/logging.h>
#include <boost/asio/ssl.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>

#include "json_rpc.h"

namespace ssl = boost::asio::ssl;

// HTTP セッションのタイムアウト時間（秒）
static constexpr int kHttpSessionTimeoutSeconds = 30;

// HTTP プロキシのタイムアウト時間（秒）
static constexpr int kHttpProxyTimeoutSeconds = 30;

// HTTPプロキシレスポンスボディの最大サイズ (10MB)
static constexpr std::size_t MAX_PROXY_RESPONSE_SIZE = 10 * 1024 * 1024;

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
      beast::bind_front_handler(&HttpServer::OnAccept, this));
}

void HttpServer::OnAccept(beast::error_code ec, tcp::socket socket) {
  if (ec) {
    RTC_LOG(LS_ERROR) << "Accept error: " << ec.message();
  } else {
    std::make_shared<HttpSession>(std::move(socket), ui_remote_url_,
                                  duckdb_writer_)
        ->Run();
  }

  if (running_) {
    DoAccept();
  }
}

http::response<http::string_body> HttpSession::HandleRequest(
    http::request<http::string_body>&& req) {
  // ヘルスチェックエンドポイント
  if (req.target() == "/.ok" && req.method() == http::verb::get) {
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = "OK";
    res.prepare_payload();
    return res;
  }

  // JSON-RPC エンドポイント
  if (req.target() == "/rpc" && req.method() == http::verb::post) {
    return HandleJsonRpcRequest(req);
  }

  // UI リモート URL が設定されている場合はリバースプロキシ
  if (!ui_remote_url_.empty()) {
    return SimpleProxyRequest(req);
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
    JsonRpcHandler handler(duckdb_writer_);
    auto response = handler.Process(json_request);

    // Notification の場合はレスポンスを返さない（空のボディで 204 No Content）
    if (!response) {
      res.result(http::status::no_content);
      res.body() = "";
      res.prepare_payload();
      return res;
    }

    res.body() = boost::json::serialize(*response);
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

  stream_.expires_after(std::chrono::seconds(kHttpSessionTimeoutSeconds));

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

http::response<http::string_body> HttpSession::SimpleProxyRequest(
    const http::request<http::string_body>& req) {
  try {
    // 簡易URLパース実装
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
      port = (scheme == "https") ? "443" : "80";
    } else {
      host = url_str;
      port = (scheme == "https") ? "443" : "80";
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
      std::string field_name(field.name_string());
      if (field_name != "Host" && field_name != "Connection" &&
          field_name != "User-Agent") {
        proxy_req.set(field_name, field.value());
      }
    }

    // ボディをコピー
    proxy_req.body() = req.body();
    proxy_req.prepare_payload();

    // HTTPSの場合はSSLを使用
    if (scheme == "https") {
      // SSLコンテキストを作成
      ssl::context ctx{ssl::context::tlsv12_client};

      // デフォルトの証明書検証を設定
      // リモート UI サーバー専用のため、正規の証明書を持つサーバーを想定
      ctx.set_verify_mode(ssl::verify_peer);
      ctx.set_default_verify_paths();

      // SSLストリームを作成（beast::tcp_stream を使用してタイムアウトを設定可能にする）
      beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
      stream.next_layer().expires_after(
          std::chrono::seconds(kHttpProxyTimeoutSeconds));

      // SNI (Server Name Indication) を設定
      if (!::SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        throw std::runtime_error("Failed to set SNI hostname");
      }

      // 同期的にDNS解決と接続
      tcp::resolver resolver(ioc);
      auto const results = resolver.resolve(host, port);
      stream.next_layer().connect(results);

      // SSL ハンドシェイク
      stream.handshake(ssl::stream_base::client);

      // リクエストを送信
      http::write(stream, proxy_req);

      // レスポンスを受信
      beast::flat_buffer buffer;
      http::response<http::dynamic_body> proxy_res;
      http::read(stream, buffer, proxy_res);

      // SSL シャットダウン
      beast::error_code ec;
      stream.shutdown(ec);
      if (ec && ec != beast::errc::not_connected) {
        RTC_LOG(LS_WARNING) << "SSL shutdown error: " << ec.message();
      }

      // レスポンスボディのサイズチェック
      std::size_t body_size = 0;
      for (auto const& buf : proxy_res.body().data()) {
        body_size += buf.size();
      }

      if (body_size > MAX_PROXY_RESPONSE_SIZE) {
        RTC_LOG(LS_ERROR) << "Proxy response too large: " << body_size
                          << " bytes";
        http::response<http::string_body> res{http::status::payload_too_large,
                                              req.version()};
        res.set(http::field::server, "Zakuro");
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(req.keep_alive());
        res.body() = "Response too large: " + std::to_string(body_size) +
                     " bytes (max: " + std::to_string(MAX_PROXY_RESPONSE_SIZE) +
                     " bytes)";
        res.prepare_payload();
        return res;
      }

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
    }

    // HTTPの場合の処理
    beast::tcp_stream stream(ioc);
    stream.expires_after(std::chrono::seconds(kHttpProxyTimeoutSeconds));

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

    // レスポンスボディのサイズチェック
    std::size_t body_size = 0;
    for (auto const& buf : proxy_res.body().data()) {
      body_size += buf.size();
    }

    if (body_size > MAX_PROXY_RESPONSE_SIZE) {
      RTC_LOG(LS_ERROR) << "Proxy response too large: " << body_size
                        << " bytes";
      http::response<http::string_body> res{http::status::payload_too_large,
                                            req.version()};
      res.set(http::field::server, "Zakuro");
      res.set(http::field::content_type, "text/plain");
      res.keep_alive(req.keep_alive());
      res.body() = "Response too large: " + std::to_string(body_size) +
                   " bytes (max: " + std::to_string(MAX_PROXY_RESPONSE_SIZE) +
                   " bytes)";
      res.prepare_payload();
      return res;
    }

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
