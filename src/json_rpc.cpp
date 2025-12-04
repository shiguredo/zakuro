#include "json_rpc.h"

#include <duckdb.h>

#include <rtc_base/logging.h>
#include <boost/json.hpp>
#include <boost/version.hpp>

#include "duckdb_stats_writer.h"
#include "zakuro_version.h"

namespace json = boost::json;

JsonRpcHandler::JsonRpcHandler(std::shared_ptr<DuckDBStatsWriter> duckdb_writer)
    : duckdb_writer_(duckdb_writer) {}

std::optional<json::object> JsonRpcHandler::Process(
    const json::value& request) {
  json::object response;
  response["jsonrpc"] = "2.0";

  // リクエストの検証
  if (!request.is_object()) {
    return CreateErrorResponse(nullptr, -32600, "Invalid Request",
                               "Request must be a JSON object");
  }

  const auto& obj = request.as_object();

  // id フィールドの取得と検証
  // JSON-RPC 2.0 では id がない場合は Notification として扱い、レスポンスを返さない
  bool is_notification = !obj.contains("id");
  json::value id = nullptr;
  if (!is_notification) {
    const auto& id_value = obj.at("id");
    // id は数値、文字列、またはnullのみ許可
    if (!id_value.is_null() && !id_value.is_number() && !id_value.is_string()) {
      return CreateErrorResponse(nullptr, -32600, "Invalid Request",
                                 "id must be a number, string, or null");
    }
    id = id_value;
  }

  // jsonrpc フィールドの確認
  if (!obj.contains("jsonrpc") || !obj.at("jsonrpc").is_string() ||
      obj.at("jsonrpc").as_string() != "2.0") {
    // Notification でもプロトコルエラーの場合はエラーを返す
    if (is_notification) {
      return std::nullopt;
    }
    return CreateErrorResponse(id, -32600, "Invalid Request",
                               "Missing or invalid jsonrpc field");
  }

  // method フィールドの確認
  if (!obj.contains("method") || !obj.at("method").is_string()) {
    if (is_notification) {
      return std::nullopt;
    }
    return CreateErrorResponse(id, -32600, "Invalid Request",
                               "Missing or invalid method field");
  }

  auto method = obj.at("method").as_string();

  // メソッドの処理
  try {
    if (method == "GetVersion") {
      // Notification の場合はレスポンスを返さない
      if (is_notification) {
        return std::nullopt;
      }
      return CreateSuccessResponse(id, HandleVersionMethod());
    } else if (method == "Query") {
      if (is_notification) {
        return std::nullopt;
      }
      auto params = obj.contains("params") ? obj.at("params") : json::object{};
      return CreateSuccessResponse(id, HandleQueryMethod(params));
    } else if (method == "ListConnections") {
      if (is_notification) {
        return std::nullopt;
      }
      auto params = obj.contains("params") ? obj.at("params") : json::object{};
      return CreateSuccessResponse(id, HandleListConnectionsMethod(params));
    } else {
      if (is_notification) {
        return std::nullopt;
      }
      return CreateErrorResponse(id, -32601, "Method not found",
                                 "Unknown method: " + std::string(method));
    }
  } catch (const JsonRpcError& e) {
    if (is_notification) {
      return std::nullopt;
    }
    return CreateErrorResponse(id, e.code, e.message, e.data);
  } catch (const std::exception& e) {
    if (is_notification) {
      return std::nullopt;
    }
    return CreateErrorResponse(id, -32603, "Internal error", e.what());
  }
}

json::object JsonRpcHandler::CreateErrorResponse(const json::value& id,
                                                 int code,
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

json::object JsonRpcHandler::CreateSuccessResponse(const json::value& id,
                                                   const json::value& result) {
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

  // DuckDBWriter が設定されていない場合
  if (!duckdb_writer_) {
    throw JsonRpcError{-32603, "Internal error", "Database not available"};
  }

  // SQL 文を取得して実行
  std::string sql(params_obj.at("sql").as_string());
  std::string result_json = duckdb_writer_->ExecuteQuery(sql);

  // ExecuteQuery の結果をパースして返す
  boost::system::error_code ec;
  auto result_jv = json::parse(result_json, ec);
  if (ec) {
    RTC_LOG(LS_ERROR) << "Failed to parse query result JSON: " << ec.message();
    throw JsonRpcError{-32603, "Internal error",
                       "Failed to parse query result"};
  }

  // エラーチェック
  if (result_jv.is_object()) {
    const auto& result_obj = result_jv.as_object();
    if (result_obj.contains("error")) {
      throw JsonRpcError{-32603, "Query execution error",
                         json::serialize(result_obj.at("error"))};
    }
  }

  return result_jv;
}

json::value JsonRpcHandler::HandleListConnectionsMethod(
    const json::value& params) {
  // DuckDBWriter が設定されていない場合
  if (!duckdb_writer_) {
    throw JsonRpcError{-32603, "Internal error", "Database not available"};
  }

  // パラメータの取得
  int64_t limit = 100;  // デフォルト値

  if (params.is_object()) {
    const auto& params_obj = params.as_object();
    if (params_obj.contains("limit")) {
      if (!params_obj.at("limit").is_int64()) {
        throw JsonRpcError{-32602, "Invalid params",
                           "params.limit must be an integer"};
      }
      limit = params_obj.at("limit").as_int64();
      if (limit < 1 || limit > 1000) {
        throw JsonRpcError{-32602, "Invalid params",
                           "params.limit must be between 1 and 1000"};
      }
    }
  }

  // 全件数を取得
  std::string count_sql = "SELECT COUNT(*) as count FROM connection";
  std::string count_result_json = duckdb_writer_->ExecuteQuery(count_sql);

  boost::system::error_code ec;
  auto count_jv = json::parse(count_result_json, ec);
  if (ec) {
    throw JsonRpcError{-32603, "Internal error",
                       "Failed to parse count result"};
  }

  int64_t total_count = 0;
  if (count_jv.is_object() && count_jv.as_object().contains("rows")) {
    const auto& rows = count_jv.at("rows").as_array();
    if (!rows.empty() && rows[0].is_object()) {
      total_count = rows[0].at("count").as_int64();
    }
  }

  // 接続一覧を取得
  std::string sql =
      "SELECT timestamp, channel_id, connection_id, session_id, "
      "role, audio, video, websocket_connected, datachannel_connected "
      "FROM connection ORDER BY timestamp DESC LIMIT " +
      std::to_string(limit);
  std::string result_json = duckdb_writer_->ExecuteQuery(sql);

  auto result_jv = json::parse(result_json, ec);
  if (ec) {
    throw JsonRpcError{-32603, "Internal error",
                       "Failed to parse query result"};
  }

  // エラーチェック
  if (result_jv.is_object()) {
    const auto& result_obj = result_jv.as_object();
    if (result_obj.contains("error")) {
      throw JsonRpcError{-32603, "Query execution error",
                         json::serialize(result_obj.at("error"))};
    }
  }

  // レスポンスを構築
  json::object result;
  if (result_jv.is_object() && result_jv.as_object().contains("rows")) {
    result["connections"] = result_jv.at("rows");
  } else {
    result["connections"] = json::array{};
  }
  result["total_count"] = total_count;

  return result;
}
