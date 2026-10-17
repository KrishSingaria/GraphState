#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace gs {
class Engine;
}

namespace gs::mcp {

struct ToolContext {
  Engine& engine;
  std::string session_id;
};

struct ToolError : public std::exception {
  std::string code;
  std::string message;
  nlohmann::json data;

  ToolError(std::string c, std::string msg, nlohmann::json d = nlohmann::json::object())
      : code(std::move(c)), message(std::move(msg)), data(std::move(d)) {}

  const char* what() const noexcept override {
    return message.c_str();
  }
};

using Handler = std::function<nlohmann::json(ToolContext&, const nlohmann::json& args)>;

struct Tool {
  std::string name;
  std::string title;
  std::string description;
  nlohmann::json input_schema;
  nlohmann::json annotations;
  Handler handler;
  std::function<std::string(const nlohmann::json&)> render; // default: compact dump
};

class ToolRegistry {
public:
  ToolRegistry();
  ~ToolRegistry() = default;

  void add(Tool tool);
  const Tool* find(std::string_view name) const;

  // Returns array of tools in the deterministic order specified in §08.5
  nlohmann::json list_json() const;

private:
  std::vector<Tool> tools_;
};

void register_read_tools(ToolRegistry& reg);
void register_admin_tools(ToolRegistry& reg);
void register_write_tools(ToolRegistry& reg);
void register_memory_tools(ToolRegistry& reg);

} // namespace gs::mcp
