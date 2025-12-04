#ifndef JSON_RPC_H_
#define JSON_RPC_H_

#include <boost/json/value.hpp>
#include <string>

class JsonRpcHandler {
 public:
  JsonRpcHandler() = default;

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

  // カスタムエラー型
  struct JsonRpcError {
    int code;
    std::string message;
    std::string data;
  };
};

#endif  // JSON_RPC_H_
