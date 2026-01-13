#include "json_rpc.h"

#include <rtc_base/logging.h>
#include <boost/json.hpp>
#include <boost/version.hpp>

#include "zakuro_version.h"

namespace json = boost::json;

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

  // 以降はどんな問題が起きても id を付与する必要があるので try-catch で囲む
  try {
    // jsonrpc フィールドの確認
    if (!obj.contains("jsonrpc") || !obj.at("jsonrpc").is_string() ||
        obj.at("jsonrpc").as_string() != "2.0") {
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
    if (method == "GetVersion") {
      // Notification の場合はレスポンスを返さない
      if (is_notification) {
        return std::nullopt;
      }
      return CreateSuccessResponse(id, HandleVersionMethod());
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
