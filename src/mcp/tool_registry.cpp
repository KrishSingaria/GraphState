#include "mcp/tool_registry.hpp"

#include <algorithm>
#include <unordered_map>

namespace gs::mcp {

namespace {

const std::vector<std::string_view> FIXED_TOOL_ORDER = {
    "get_briefing",
    "search_symbols",
    "get_file_outline",
    "read_symbol",
    "get_impact",
    "get_module_map",
    "propose_patch",
    "remember",
    "recall",
    "recall_pitfalls",
    "end_session",
    "reindex",
    "status",
    "changeset"
};

} // namespace

ToolRegistry::ToolRegistry() = default;

void ToolRegistry::add(Tool tool) {
  if (!tool.render) {
    tool.render = [](const nlohmann::json& j) {
      return j.dump();
    };
  }

  for (auto& existing : tools_) {
    if (existing.name == tool.name) {
      existing = std::move(tool);
      return;
    }
  }
  tools_.push_back(std::move(tool));
}

const Tool* ToolRegistry::find(std::string_view name) const {
  for (const auto& t : tools_) {
    if (t.name == name) {
      return &t;
    }
  }
  return nullptr;
}

nlohmann::json ToolRegistry::list_json() const {
  std::vector<const Tool*> ordered;
  for (const auto& name : FIXED_TOOL_ORDER) {
    const Tool* t = find(name);
    if (t) {
      ordered.push_back(t);
    }
  }

  // Any extra tools
  for (const auto& t : tools_) {
    if (std::find(ordered.begin(), ordered.end(), &t) == ordered.end()) {
      ordered.push_back(&t);
    }
  }

  nlohmann::json list = nlohmann::json::array();
  for (const auto* t : ordered) {
    nlohmann::json item = {
        {"name", t->name},
        {"title", t->title},
        {"description", t->description},
        {"inputSchema", t->input_schema},
        {"annotations", t->annotations}
    };
    list.push_back(std::move(item));
  }

  return list;
}




} // namespace gs::mcp
