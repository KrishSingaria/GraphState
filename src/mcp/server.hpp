#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "core/engine.hpp"
#include "mcp/tool_registry.hpp"
#include "mcp/transport.hpp"

namespace gs::mcp {

enum class Era {
  Unknown,
  Legacy,
  Modern
};

class Server {
public:
  Server(Engine& engine, Transport& transport);
  ~Server() = default;

  // Run the message processing loop until EOF or stop requested
  void run();

  // Handle a single parsed JSON message.
  // Returns std::nullopt for notifications.
  std::optional<nlohmann::json> handle_message(const nlohmann::json& msg);

  Era era() const { return era_; }

private:
  Engine& engine_;
  Transport& transport_;
  ToolRegistry registry_;
  Era era_ = Era::Unknown;
  std::string session_id_;

  nlohmann::json on_initialize(const nlohmann::json& id, const nlohmann::json& params);
  nlohmann::json on_discover(const nlohmann::json& id, const nlohmann::json& params);
  nlohmann::json on_tools_list(const nlohmann::json& id);
  nlohmann::json on_tools_call(const nlohmann::json& id, const nlohmann::json& params);
  nlohmann::json on_resources_list(const nlohmann::json& id);
  nlohmann::json on_resources_read(const nlohmann::json& id, const nlohmann::json& params);
  nlohmann::json on_prompts_list(const nlohmann::json& id);
  nlohmann::json on_prompts_get(const nlohmann::json& id, const nlohmann::json& params);

  nlohmann::json decorate_result(nlohmann::json res, bool is_list_or_read) const;
  nlohmann::json make_error(const nlohmann::json& id, int code, std::string_view msg, nlohmann::json data = nullptr);
};

} // namespace gs::mcp
