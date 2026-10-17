#include "mcp/server.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "core/clock.hpp"
#include "core/hash.hpp"
#include "mcp/instructions.inc"
#include "mcp/schema_validate.hpp"
#include "memory/sessions.hpp"

#ifndef GS_VERSION
#define GS_VERSION "0.1.0"
#endif

namespace gs::mcp {

// External declarations from resources.cpp and prompts.cpp
nlohmann::json handle_resources_list();
nlohmann::json handle_resources_read(Engine& engine, std::string_view uri);
nlohmann::json handle_prompts_list();
nlohmann::json handle_prompts_get(std::string_view name, const nlohmann::json& arguments);

namespace {

const std::vector<std::string> SUPPORTED_LEGACY_VERSIONS = {
    "2025-11-25",
    "2025-06-18",
    "2025-03-26",
    "2024-11-05"
};

const std::vector<std::string> ALL_SUPPORTED_VERSIONS = {
    "2026-07-28",
    "2025-11-25",
    "2025-06-18",
    "2025-03-26",
    "2024-11-05"
};

} // namespace

Server::Server(Engine& engine, Transport& transport)
    : engine_(engine), transport_(transport), session_id_(engine_.sessions().ctx().session_id) {
  register_read_tools(registry_);
  register_admin_tools(registry_);
  register_write_tools(registry_);
  register_memory_tools(registry_);
}

void Server::run() {
  while (auto line = transport_.read_line()) {
    nlohmann::json msg;
    try {
      msg = nlohmann::json::parse(*line);
    } catch (const nlohmann::json::parse_error&) {
      transport_.send_json(make_error(nullptr, -32700, "Parse error"));
      continue;
    }

    auto resp = handle_message(msg);
    if (resp.has_value()) {
      transport_.send_json(*resp);
    }
  }

  engine_.sessions().end_on_shutdown();
}

std::optional<nlohmann::json> Server::handle_message(const nlohmann::json& msg) {
  // Batching is not supported by MCP (top-level array -> -32600)
  if (msg.is_array()) {
    return make_error(nullptr, -32600, "JSON-RPC batching is not supported");
  }

  if (!msg.is_object() || !msg.contains("jsonrpc") || msg["jsonrpc"] != "2.0") {
    nlohmann::json id = msg.is_object() && msg.contains("id") ? msg["id"] : nullptr;
    return make_error(id, -32600, "Invalid Request");
  }

  // Notifications (no id field)
  if (!msg.contains("id") || msg["id"].is_null()) {
    // Process notification, do not reply
    return std::nullopt;
  }

  const auto& id = msg["id"];

  if (!msg.contains("method") || !msg["method"].is_string()) {
    return make_error(id, -32600, "Missing or invalid method");
  }

  std::string method = msg["method"].get<std::string>();
  nlohmann::json params = msg.value("params", nlohmann::json::object());

  // Check Modern Era meta version if present
  if (params.is_object() && params.contains("_meta") && params["_meta"].is_object()) {
    era_ = Era::Modern;
    const auto& meta = params["_meta"];
    if (meta.contains("io.modelcontextprotocol/protocolVersion")) {
      std::string requested = meta["io.modelcontextprotocol/protocolVersion"].get<std::string>();
      if (requested != "2026-07-28") {
        return make_error(id, -32022, "Unsupported protocol version", {
            {"supported", ALL_SUPPORTED_VERSIONS},
            {"requested", requested}
        });
      }
    }
  }

  // Method dispatch
  if (method == "initialize") {
    return on_initialize(id, params);
  }
  if (method == "server/discover") {
    return on_discover(id, params);
  }
  if (method == "ping") {
    return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", nlohmann::json::object()}}, false);
  }
  if (method == "tools/list") {
    return on_tools_list(id);
  }
  if (method == "tools/call") {
    return on_tools_call(id, params);
  }
  if (method == "resources/list") {
    return on_resources_list(id);
  }
  if (method == "resources/read") {
    return on_resources_read(id, params);
  }
  if (method == "prompts/list") {
    return on_prompts_list(id);
  }
  if (method == "prompts/get") {
    return on_prompts_get(id, params);
  }
  if (method == "logging/setLevel") {
    return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", nlohmann::json::object()}}, false);
  }

  return make_error(id, -32601, "Method not found: " + method);
}

nlohmann::json Server::on_initialize(const nlohmann::json& id, const nlohmann::json& params) {
  era_ = Era::Legacy;

  if (params.is_object() && params.contains("clientInfo")) {
    engine_.sessions().ensure(params["clientInfo"]);
  } else {
    engine_.sessions().ensure(nlohmann::json::object());
  }
  session_id_ = engine_.sessions().ctx().session_id;

  std::string requested_version = "2025-11-25";
  if (params.is_object() && params.contains("protocolVersion") && params["protocolVersion"].is_string()) {
    std::string v = params["protocolVersion"].get<std::string>();
    for (const auto& sup : SUPPORTED_LEGACY_VERSIONS) {
      if (sup == v) {
        requested_version = v;
        break;
      }
    }
  }

  nlohmann::json result = {
      {"protocolVersion", requested_version},
      {"capabilities", {
          {"tools", {{"listChanged", false}}},
          {"resources", {{"subscribe", false}, {"listChanged", false}}},
          {"prompts", {{"listChanged", false}}}
      }},
      {"serverInfo", {
          {"name", "graphstate"},
          {"title", "GraphState"},
          {"version", GS_VERSION}
      }},
      {"instructions", SERVER_INSTRUCTIONS}
  };

  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

nlohmann::json Server::on_discover(const nlohmann::json& id, const nlohmann::json&) {
  era_ = Era::Modern;

  engine_.sessions().ensure(nlohmann::json{{"name", "modern-client"}});
  session_id_ = engine_.sessions().ctx().session_id;

  nlohmann::json result = {
      {"resultType", "complete"},
      {"supportedVersions", ALL_SUPPORTED_VERSIONS},
      {"capabilities", {
          {"tools", nlohmann::json::object()},
          {"resources", nlohmann::json::object()},
          {"prompts", nlohmann::json::object()}
      }},
      {"_meta", {
          {"io.modelcontextprotocol/serverInfo", {
              {"name", "graphstate"},
              {"title", "GraphState"},
              {"version", GS_VERSION}
          }}
      }},
      {"instructions", SERVER_INSTRUCTIONS},
      {"ttlMs", 3600000},
      {"cacheScope", "private"}
  };

  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

nlohmann::json Server::on_tools_list(const nlohmann::json& id) {
  nlohmann::json tools_array = registry_.list_json();
  nlohmann::json result = {{"tools", tools_array}};
  return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}, true);
}

nlohmann::json Server::on_tools_call(const nlohmann::json& id, const nlohmann::json& params) {
  if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
    return make_error(id, -32602, "Invalid tool call params: missing 'name'");
  }

  std::string name = params["name"].get<std::string>();
  const Tool* tool = registry_.find(name);
  if (!tool) {
    return make_error(id, -32602, "Unknown tool: " + name);
  }

  nlohmann::json args = nlohmann::json::object();
  if (params.contains("arguments")) {
    if (!params["arguments"].is_object()) {
      return make_error(id, -32602, "Invalid arguments: expected object");
    }
    args = params["arguments"];
  }

  // Schema validation
  auto val_errors = validate_schema(tool->input_schema, args);
  if (!val_errors.empty()) {
    std::string err_msg = "Invalid argument: " + val_errors[0].format();
    nlohmann::json tool_err_res = {
        {"content", {{{"type", "text"}, {"text", "ERROR INVALID_ARGUMENT: " + err_msg}}}},
        {"structuredContent", {
            {"error", {
                {"code", "INVALID_ARGUMENT"},
                {"message", err_msg},
                {"data", {{"path", val_errors[0].path}}}
            }}
        }},
        {"isError", true}
    };
    return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", tool_err_res}}, false);
  }

  engine_.sessions().heartbeat();
  ToolContext ctx{engine_, engine_.sessions().ctx(), engine_.sessions().ctx().session_id};
  nlohmann::json tool_result;

  try {
    std::unique_lock<std::mutex> lock(engine_.state_mutex());
    nlohmann::json structured = tool->handler(ctx, args);
    std::string rendered = tool->render ? tool->render(structured) : structured.dump();

    tool_result = {
        {"content", {{{"type", "text"}, {"text", rendered}}}},
        {"structuredContent", structured},
        {"isError", false}
    };
  } catch (const ToolError& e) {
    tool_result = {
        {"content", {{{"type", "text"}, {"text", "ERROR " + e.code + ": " + e.message}}}},
        {"structuredContent", {
            {"error", {
                {"code", e.code},
                {"message", e.message},
                {"data", e.data}
            }}
        }},
        {"isError", true}
    };
  } catch (const std::exception& e) {
    tool_result = {
        {"content", {{{"type", "text"}, {"text", std::string("ERROR INTERNAL: ") + e.what()}}}},
        {"structuredContent", {
            {"error", {
                {"code", "INTERNAL"},
                {"message", e.what()},
                {"data", nlohmann::json::object()}
            }}
        }},
        {"isError", true}
    };
  }

  if (engine_.has_notices()) {
    auto notices = engine_.drain_notices();
    if (!notices.empty() && tool_result.contains("structuredContent") && tool_result["structuredContent"].is_object()) {
      tool_result["structuredContent"]["_notices"] = std::move(notices);
    }
  }

  return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", tool_result}}, false);
}

nlohmann::json Server::on_resources_list(const nlohmann::json& id) {
  nlohmann::json res = handle_resources_list();
  return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", res}}, true);
}

nlohmann::json Server::on_resources_read(const nlohmann::json& id, const nlohmann::json& params) {
  if (!params.is_object() || !params.contains("uri") || !params["uri"].is_string()) {
    return make_error(id, -32602, "Invalid params: missing 'uri'");
  }

  std::string uri = params["uri"].get<std::string>();
  nlohmann::json read_res = handle_resources_read(engine_, uri);
  if (read_res.is_null()) {
    int code = (era_ == Era::Modern) ? -32602 : -32002;
    return make_error(id, code, "Resource not found: " + uri, {{"uri", uri}});
  }

  return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", read_res}}, true);
}

nlohmann::json Server::on_prompts_list(const nlohmann::json& id) {
  nlohmann::json res = handle_prompts_list();
  return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", res}}, true);
}

nlohmann::json Server::on_prompts_get(const nlohmann::json& id, const nlohmann::json& params) {
  if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
    return make_error(id, -32602, "Invalid params: missing 'name'");
  }

  std::string name = params["name"].get<std::string>();
  nlohmann::json args = params.value("arguments", nlohmann::json::object());

  nlohmann::json get_res = handle_prompts_get(name, args);
  if (get_res.is_null()) {
    return make_error(id, -32602, "Prompt not found: " + name);
  }

  return decorate_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", get_res}}, false);
}

nlohmann::json Server::decorate_result(nlohmann::json res, bool is_list_or_read) const {
  if (era_ == Era::Modern && res.contains("result") && res["result"].is_object()) {
    res["result"]["resultType"] = "complete";
    if (is_list_or_read) {
      res["result"]["ttlMs"] = 3600000;
      res["result"]["cacheScope"] = "private";
    }
  }
  return res;
}

nlohmann::json Server::make_error(const nlohmann::json& id, int code, std::string_view msg, nlohmann::json data) {
  nlohmann::json err = {
      {"code", code},
      {"message", std::string(msg)}
  };
  if (!data.is_null()) {
    err["data"] = std::move(data);
  }
  return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", std::move(err)}
  };
}

} // namespace gs::mcp
