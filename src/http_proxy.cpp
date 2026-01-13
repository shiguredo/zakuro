#include "http_proxy.h"

#include <chrono>
#include <string>
#include <utility>

#include <openssl/ssl.h>

#include <boost/asio/ssl.hpp>
#include <boost/beast/http.hpp>

#include <rtc_base/logging.h>

namespace {
constexpr int kHttpProxyTimeoutSeconds = 30;
constexpr std::size_t kMaxProxyResponseSize = 10 * 1024 * 1024;
}  // namespace

HttpProxy::HttpProxy(boost::asio::any_io_executor executor,
                     const std::string& ui_remote_url)
    : executor_(executor), resolver_(executor), ui_remote_url_(ui_remote_url) {}

void HttpProxy::AsyncHandleRequest(
    boost::beast::http::request<boost::beast::http::string_body> req,
    std::function<
        void(boost::beast::http::response<boost::beast::http::string_body>)>
        on_response) {
  namespace http = boost::beast::http;

  on_response_ = std::move(on_response);
  req_version_ = req.version();
  keep_alive_ = req.keep_alive();

  if (!ParseUrl(ui_remote_url_)) {
    SendErrorResponse("Invalid proxy URL");
    return;
  }

  proxy_req_ = http::request<http::string_body>{req.method(), req.target(),
                                                req.version()};
  proxy_req_.set(http::field::host, host_header_);
  proxy_req_.set(http::field::user_agent, "Zakuro/1.0");

  for (auto const& field : req) {
    if (field.name() != http::field::host &&
        field.name() != http::field::connection &&
        field.name() != http::field::user_agent) {
      proxy_req_.set(field.name(), field.value());
    }
  }

  proxy_req_.body() = std::move(req.body());
  proxy_req_.prepare_payload();

  DoResolve();
}

void HttpProxy::DoResolve() {
  resolver_.async_resolve(
      host_, port_,
      [self = shared_from_this()](
          boost::beast::error_code ec,
          boost::asio::ip::tcp::resolver::results_type results) {
        self->OnResolve(ec, std::move(results));
      });
}

void HttpProxy::OnResolve(
    boost::beast::error_code ec,
    boost::asio::ip::tcp::resolver::results_type results) {
  if (ec) {
    SendErrorResponse("Resolve error: " + ec.message());
    return;
  }

  if (use_ssl_) {
    namespace ssl = boost::asio::ssl;

    ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
    ssl_ctx_->set_default_verify_paths();

    ssl_stream_ =
        std::make_unique<boost::beast::ssl_stream<boost::beast::tcp_stream>>(
            boost::beast::tcp_stream(executor_), *ssl_ctx_);

    if (!::SSL_set_tlsext_host_name(ssl_stream_->native_handle(),
                                    host_.c_str())) {
      SendErrorResponse("Failed to set SNI hostname");
      return;
    }

    SetTimeout();
    boost::beast::get_lowest_layer(*ssl_stream_)
        .async_connect(results,
                       [self = shared_from_this()](
                           boost::beast::error_code ec,
                           const boost::asio::ip::tcp::endpoint& endpoint) {
                         boost::ignore_unused(endpoint);
                         self->OnConnect(ec);
                       });
  } else {
    stream_ = std::make_unique<boost::beast::tcp_stream>(executor_);
    SetTimeout();
    stream_->async_connect(results,
                           [self = shared_from_this()](
                               boost::beast::error_code ec,
                               const boost::asio::ip::tcp::endpoint& endpoint) {
                             boost::ignore_unused(endpoint);
                             self->OnConnect(ec);
                           });
  }
}

void HttpProxy::OnConnect(boost::beast::error_code ec) {
  if (ec) {
    SendErrorResponse("Connect error: " + ec.message());
    return;
  }

  if (use_ssl_) {
    namespace ssl = boost::asio::ssl;

    SetTimeout();
    ssl_stream_->async_handshake(
        ssl::stream_base::client,
        [self = shared_from_this()](boost::beast::error_code ec) {
          self->OnSslHandshake(ec);
        });
  } else {
    SetTimeout();
    boost::beast::http::async_write(
        *stream_, proxy_req_,
        [self = shared_from_this()](boost::beast::error_code ec,
                                    std::size_t bytes_transferred) {
          self->OnWrite(ec, bytes_transferred);
        });
  }
}

void HttpProxy::OnSslHandshake(boost::beast::error_code ec) {
  if (ec) {
    SendErrorResponse("SSL handshake error: " + ec.message());
    return;
  }

  SetTimeout();
  boost::beast::http::async_write(
      *ssl_stream_, proxy_req_,
      [self = shared_from_this()](boost::beast::error_code ec,
                                  std::size_t bytes_transferred) {
        self->OnWrite(ec, bytes_transferred);
      });
}

void HttpProxy::OnWrite(boost::beast::error_code ec,
                        std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    SendErrorResponse("Write error: " + ec.message());
    return;
  }

  SetTimeout();
  if (use_ssl_) {
    boost::beast::http::async_read(
        *ssl_stream_, buffer_, proxy_res_,
        [self = shared_from_this()](boost::beast::error_code ec,
                                    std::size_t bytes_transferred) {
          self->OnRead(ec, bytes_transferred);
        });
  } else {
    boost::beast::http::async_read(
        *stream_, buffer_, proxy_res_,
        [self = shared_from_this()](boost::beast::error_code ec,
                                    std::size_t bytes_transferred) {
          self->OnRead(ec, bytes_transferred);
        });
  }
}

void HttpProxy::OnRead(boost::beast::error_code ec,
                       std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    SendErrorResponse("Read error: " + ec.message());
    return;
  }

  if (use_ssl_) {
    ssl_stream_->async_shutdown(
        [self = shared_from_this()](boost::beast::error_code ec) {
          self->OnShutdown(ec);
        });
  } else {
    FinishResponse();

    boost::beast::error_code close_ec;
    stream_->socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                               close_ec);
  }
}

void HttpProxy::OnShutdown(boost::beast::error_code ec) {
  FinishResponse();
  if (ec && ec != boost::asio::ssl::error::stream_truncated) {
    RTC_LOG(LS_WARNING) << "SSL shutdown error: " << ec.message();
  }
}

void HttpProxy::FinishResponse() {
  namespace http = boost::beast::http;

  std::size_t body_size = boost::beast::buffer_bytes(proxy_res_.body().data());
  if (body_size > kMaxProxyResponseSize) {
    RTC_LOG(LS_ERROR) << "Proxy response too large: " << body_size << " bytes";
    http::response<http::string_body> res{http::status::payload_too_large,
                                          req_version_};
    res.set(http::field::server, "Zakuro");
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(keep_alive_);
    res.body() = "Response too large: " + std::to_string(body_size) +
                 " bytes (max: " + std::to_string(kMaxProxyResponseSize) +
                 " bytes)";
    res.prepare_payload();
    if (on_response_) {
      std::move(on_response_)(std::move(res));
      on_response_ = nullptr;
    }
    return;
  }

  std::string body_str =
      boost::beast::buffers_to_string(proxy_res_.body().data());

  http::response<http::string_body> res{proxy_res_.result(), req_version_};
  res.set(http::field::server, "Zakuro");

  for (auto const& field : proxy_res_) {
    if (field.name() != http::field::connection &&
        field.name() != http::field::transfer_encoding &&
        field.name() != http::field::server) {
      res.set(field.name(), field.value());
    }
  }

  res.body() = std::move(body_str);
  res.keep_alive(keep_alive_);
  res.prepare_payload();

  if (on_response_) {
    std::move(on_response_)(std::move(res));
    on_response_ = nullptr;
  }
}

void HttpProxy::SendErrorResponse(const std::string& message) {
  namespace http = boost::beast::http;

  RTC_LOG(LS_ERROR) << "Proxy error: " << message;
  http::response<http::string_body> res{http::status::bad_gateway,
                                        req_version_};
  res.set(http::field::server, "Zakuro");
  res.set(http::field::content_type, "text/plain");
  res.keep_alive(keep_alive_);
  res.body() = "Proxy Error: " + message;
  res.prepare_payload();

  if (on_response_) {
    std::move(on_response_)(std::move(res));
    on_response_ = nullptr;
  }
}

bool HttpProxy::ParseUrl(const std::string& url) {
  std::string url_str = url;
  std::string scheme = "http";
  std::string host;
  std::string port = "80";

  if (url_str.rfind("https://", 0) == 0) {
    scheme = "https";
    port = "443";
    url_str = url_str.substr(8);
  } else if (url_str.rfind("http://", 0) == 0) {
    url_str = url_str.substr(7);
  }

  const std::size_t port_pos = url_str.find(':');
  const std::size_t path_pos = url_str.find('/');

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

  if (host.empty() || port.empty()) {
    return false;
  }

  host_ = host;
  port_ = port;
  use_ssl_ = (scheme == "https");
  host_header_ =
      (port_ == "80" || port_ == "443") ? host_ : host_ + ":" + port_;
  return true;
}

void HttpProxy::SetTimeout() {
  if (use_ssl_) {
    boost::beast::get_lowest_layer(*ssl_stream_)
        .expires_after(std::chrono::seconds(kHttpProxyTimeoutSeconds));
  } else {
    stream_->expires_after(std::chrono::seconds(kHttpProxyTimeoutSeconds));
  }
}
