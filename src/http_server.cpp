#include "http_server.h"

#include <chrono>
#include <string>

#include <rtc_base/logging.h>

// HTTP セッションのタイムアウト時間（秒）
static constexpr int kHttpSessionTimeoutSeconds = 30;

// ----------------------------
// HttpServer
// ----------------------------

HttpServer::HttpServer(const std::string& host, int port)
    : host_(host), port_(port), resolver_(ioc_) {}

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
  assert(thread_ != nullptr);

  running_ = false;
  ioc_.stop();

  thread_->join();
  thread_ = nullptr;
}

void HttpServer::Run() {
  try {
    resolver_.async_resolve(
        host_, std::to_string(port_),
        [this](boost::beast::error_code ec,
               boost::asio::ip::tcp::resolver::results_type results) {
          OnResolve(ec, std::move(results));
        });

    ioc_.run();
  } catch (const std::exception& e) {
    RTC_LOG(LS_ERROR) << "HTTP server error: " << e.what();
  }
}

void HttpServer::OnResolve(
    boost::beast::error_code ec,
    boost::asio::ip::tcp::resolver::results_type results) {
  if (ec) {
    RTC_LOG(LS_ERROR) << "Resolve error: " << ec.message();
    return;
  }

  if (results.empty()) {
    RTC_LOG(LS_ERROR) << "Resolve error: no endpoints found";
    return;
  }

  const auto endpoint = results.begin()->endpoint();
  acceptor_.reset(new boost::asio::ip::tcp::acceptor(ioc_, endpoint));
  DoAccept();
}

void HttpServer::DoAccept() {
  acceptor_->async_accept(
      boost::beast::bind_front_handler(&HttpServer::OnAccept, this));
}

void HttpServer::OnAccept(boost::beast::error_code ec,
                          boost::asio::ip::tcp::socket socket) {
  if (ec) {
    RTC_LOG(LS_ERROR) << "Accept error: " << ec.message();
  } else {
    std::make_shared<HttpSession>(std::move(socket))->Run();
  }

  if (running_) {
    DoAccept();
  }
}

// ----------------------------
// HttpSession
// ----------------------------

HttpSession::HttpSession(boost::asio::ip::tcp::socket socket)
    : stream_(std::move(socket)) {}

boost::beast::http::response<boost::beast::http::string_body>
HttpSession::HandleRequest(
    boost::beast::http::request<boost::beast::http::string_body> req) {
  // すべてのリクエストに 404 Not Found を返す
  boost::beast::http::response<boost::beast::http::string_body> res{
      boost::beast::http::status::not_found, req.version()};
  res.set(boost::beast::http::field::server, "Zakuro");
  res.set(boost::beast::http::field::content_type, "text/plain");
  res.keep_alive(req.keep_alive());
  res.body() = "Not Found";
  res.prepare_payload();
  return res;
}

// HttpSession の実装

void HttpSession::Run() {
  boost::asio::post(stream_.get_executor(),
                    [self = shared_from_this()]() { self->DoRead(); });
}

void HttpSession::DoRead() {
  req_ = {};

  stream_.expires_after(std::chrono::seconds(kHttpSessionTimeoutSeconds));

  boost::beast::http::async_read(
      stream_, buffer_, req_,
      [self = shared_from_this()](boost::beast::error_code ec,
                                  std::size_t bytes_transferred) {
        self->OnRead(ec, bytes_transferred);
      });
}

void HttpSession::OnRead(boost::beast::error_code ec,
                         std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec == boost::beast::http::error::end_of_stream) {
    return DoClose();
  }

  if (ec) {
    RTC_LOG(LS_ERROR) << "Read error: " << ec.message();
    return;
  }

  // リクエストを処理
  SendResponse(HttpSession::HandleRequest(std::move(req_)));
}

void HttpSession::SendResponse(
    boost::beast::http::response<boost::beast::http::string_body> res) {
  res_ = std::make_shared<
      boost::beast::http::response<boost::beast::http::string_body>>(
      std::move(res));

  boost::beast::http::async_write(
      stream_, *res_,
      [self = shared_from_this()](boost::beast::error_code ec,
                                  std::size_t bytes_transferred) {
        self->OnWrite(self->res_->keep_alive(), ec, bytes_transferred);
      });
}

void HttpSession::OnWrite(bool keep_alive,
                          boost::beast::error_code ec,
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
  boost::beast::error_code ec;
  stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
}
