#ifndef JSON_RPC_H_
#define JSON_RPC_H_

#include <boost/json/value.hpp>
#include <memory>
#include <string>

class DuckDBStatsWriter;

class JsonRpcHandler {
 public:
  explicit JsonRpcHandler(std::shared_ptr<DuckDBStatsWriter> duckdb_writer);

  // JSON-RPC リクエストを処理して、レスポンスを返す
  boost::json::object Process(const boost::json::value& request);

 private:
  // エラーレスポンスを作成
  boost::json::object CreateErrorResponse(const boost::json::value& id,
                                          int code,
                                          const std::string& message,
                                          const std::string& data = "");

  // 成功レスポンスを作成
  boost::json::object CreateSuccessResponse(const boost::json::value& id,
                                            const boost::json::value& result);

  // 各メソッドのハンドラー
  boost::json::value HandleVersionMethod();
  boost::json::value HandleQueryMethod(const boost::json::value& params);

  // カスタムエラー型
  struct JsonRpcError {
    int code;
    std::string message;
    std::string data;
  };

  std::shared_ptr<DuckDBStatsWriter> duckdb_writer_;
};

#endif  // JSON_RPC_H_