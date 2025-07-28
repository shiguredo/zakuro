#include "json_rpc.h"

#include <boost/json.hpp>
#include <boost/version.hpp>
#include <duckdb.h>
#include <rtc_base/logging.h>

#include "duckdb_stats_writer.h"
#include "zakuro_version.h"

namespace json = boost::json;

JsonRpcHandler::JsonRpcHandler(std::shared_ptr<DuckDBStatsWriter> duckdb_writer)
    : duckdb_writer_(duckdb_writer) {}

json::object JsonRpcHandler::Process(const json::value& request) {
  json::object response;
  response["jsonrpc"] = "2.0";
  
  // リクエストの検証
  if (!request.is_object()) {
    return CreateErrorResponse(nullptr, -32600, "Invalid Request", "Request must be a JSON object");
  }
  
  const auto& obj = request.as_object();
  
  // id フィールドの取得と検証
  json::value id = nullptr;
  if (obj.contains("id")) {
    const auto& id_value = obj.at("id");
    // id は数値、文字列、またはnullのみ許可
    if (!id_value.is_null() && !id_value.is_number() && !id_value.is_string()) {
      return CreateErrorResponse(nullptr, -32600, "Invalid Request", "id must be a number, string, or null");
    }
    id = id_value;
  }
  
  // jsonrpc フィールドの確認
  if (!obj.contains("jsonrpc") || !obj.at("jsonrpc").is_string() || obj.at("jsonrpc").as_string() != "2.0") {
    return CreateErrorResponse(id, -32600, "Invalid Request", "Missing or invalid jsonrpc field");
  }
  
  // method フィールドの確認
  if (!obj.contains("method") || !obj.at("method").is_string()) {
    return CreateErrorResponse(id, -32600, "Invalid Request", "Missing or invalid method field");
  }
  
  auto method = obj.at("method").as_string();
  
  // メソッドの処理
  try {
    if (method == "version") {
      return CreateSuccessResponse(id, HandleVersionMethod());
    } else if (method == "query") {
      auto params = obj.contains("params") ? obj.at("params") : json::object{};
      return CreateSuccessResponse(id, HandleQueryMethod(params));
    } else {
      return CreateErrorResponse(id, -32601, "Method not found", 
                                "Unknown method: " + std::string(method));
    }
  } catch (const JsonRpcError& e) {
    return CreateErrorResponse(id, e.code, e.message, e.data);
  } catch (const std::exception& e) {
    return CreateErrorResponse(id, -32603, "Internal error", e.what());
  }
}

json::object JsonRpcHandler::CreateErrorResponse(const json::value& id, int code, 
                                                 const std::string& message, 
                                                 const std::string& data) {
  json::object response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  
  json::object error;
  error["code"] = code;
  error["message"] = message;
  if (!data.empty()) {
    error["data"] = data;
  }
  response["error"] = error;
  
  return response;
}

json::object JsonRpcHandler::CreateSuccessResponse(const json::value& id, const json::value& result) {
  json::object response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["result"] = result;
  return response;
}

json::value JsonRpcHandler::HandleVersionMethod() {
  json::object result;
  
  // Zakuro のバージョン情報
  result["zakuro"] = ZakuroVersion::GetVersion();
  
  // DuckDB のバージョン情報
  result["duckdb"] = duckdb_library_version();
  
  // Sora C++ SDK のバージョン情報
  result["sora_cpp_sdk"] = ZakuroVersion::GetSoraCppSdkVersion();
  
  // libwebrtc のバージョン情報
  result["libwebrtc"] = ZakuroVersion::GetWebRTCVersion();
  
  // Boost のバージョン情報
  result["boost"] = std::to_string(BOOST_VERSION / 100000) + "." +
                   std::to_string((BOOST_VERSION / 100) % 1000) + "." +
                   std::to_string(BOOST_VERSION % 100);
  
  return result;
}

json::value JsonRpcHandler::HandleQueryMethod(const json::value& params) {
  // パラメータの検証
  if (!params.is_object()) {
    throw JsonRpcError{-32602, "Invalid params", "params must be an object"};
  }
  
  const auto& params_obj = params.as_object();
  if (!params_obj.contains("sql") || !params_obj.at("sql").is_string()) {
    throw JsonRpcError{-32602, "Invalid params", "params.sql must be a string"};
  }
  
  // DuckDBWriterが設定されていない場合
  if (!duckdb_writer_) {
    throw JsonRpcError{-32603, "Internal error", "Database not available"};
  }
  
  // SQL文を取得して実行
  std::string sql(params_obj.at("sql").as_string());
  RTC_LOG(LS_INFO) << "DEBUG!!! JsonRpcHandler executing query: " << sql;
  RTC_LOG(LS_INFO) << "DEBUG!!! DuckDBWriter pointer: " << duckdb_writer_.get();
  std::string result_json = duckdb_writer_->ExecuteQuery(sql);
  RTC_LOG(LS_INFO) << "DEBUG!!! Query result: " << result_json;
  RTC_LOG(LS_INFO) << "DEBUG!!! Query result length: " << result_json.length();
  
  // ExecuteQueryの結果をパースして返す
  boost::system::error_code ec;
  auto result_jv = json::parse(result_json, ec);
  if (ec) {
    RTC_LOG(LS_ERROR) << "DEBUG!!! Failed to parse JSON: " << ec.message();
    RTC_LOG(LS_ERROR) << "DEBUG!!! JSON string was: " << result_json;
    throw JsonRpcError{-32603, "Internal error", "Failed to parse query result"};
  }
  RTC_LOG(LS_INFO) << "DEBUG!!! Parsed JSON successfully";
  
  // エラーチェック
  if (result_jv.is_object()) {
    const auto& result_obj = result_jv.as_object();
    if (result_obj.contains("error")) {
      RTC_LOG(LS_ERROR) << "DEBUG!!! Query execution error found";
      throw JsonRpcError{-32603, "Query execution error", 
                        json::serialize(result_obj.at("error"))};
    }
    RTC_LOG(LS_INFO) << "DEBUG!!! No error in result object";
  }
  
  RTC_LOG(LS_INFO) << "DEBUG!!! Returning result_jv";
  return result_jv;
}