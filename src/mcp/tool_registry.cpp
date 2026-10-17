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


void register_memory_tools(ToolRegistry& reg) {
  reg.add(Tool{
      .name = "get_briefing",
      .title = "Session briefing",
      .description = "Call FIRST in every session. Returns user preferences, project conventions, unfinished work from earlier sessions, known pitfalls, recent changes and enforced rules. Pass your goal to record it and get related memories.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "goal": {"type": "string", "minLength": 1, "maxLength": 500}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext&, const nlohmann::json&) -> nlohmann::json {
        throw ToolError("NOT_IMPLEMENTED", "get_briefing is implemented in milestone M6");
      }
  });

  reg.add(Tool{
      .name = "remember",
      .title = "Remember",
      .description = "Save a durable memory: a user preference (follows the user across projects), a project convention, a fact, a todo or a warning. Attach it to symbols/files with 'about'. Never store secrets.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "required": ["content", "kind"],
        "properties": {
          "content": {"type": "string", "minLength": 1, "maxLength": 2000},
          "kind": {"type": "string", "enum": ["preference", "convention", "fact", "todo", "warning"]},
          "scope": {"type": "string", "enum": ["user", "project"]},
          "about": {"type": "array", "items": {"type": "string", "maxLength": 500}, "maxItems": 20},
          "tags": {"type": "array", "items": {"type": "string", "maxLength": 50}, "maxItems": 10},
          "importance": {"type": "integer", "minimum": 1, "maximum": 3, "default": 2},
          "supersedes": {"type": "string", "maxLength": 20}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext&, const nlohmann::json&) -> nlohmann::json {
        throw ToolError("NOT_IMPLEMENTED", "remember is implemented in milestone M6");
      }
  });

  reg.add(Tool{
      .name = "recall",
      .title = "Recall memory",
      .description = "Search past memories, decisions, pitfalls, intents and sessions by words, or everything attached to a symbol/file.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "query": {"type": "string", "maxLength": 500},
          "types": {"type": "array", "items": {"type": "string", "enum": ["memory", "decision", "pitfall", "session", "intent"]}},
          "scope": {"type": "string", "enum": ["all", "project", "user"], "default": "all"},
          "about": {"type": "string", "maxLength": 500},
          "limit": {"type": "integer", "minimum": 1, "maximum": 50, "default": 10}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext&, const nlohmann::json&) -> nlohmann::json {
        throw ToolError("NOT_IMPLEMENTED", "recall is implemented in milestone M6");
      }
  });

  reg.add(Tool{
      .name = "recall_pitfalls",
      .title = "Known pitfalls",
      .description = "Known mistakes recorded from rejected patches, most frequent first. Filter by symbol, file or violation code.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "symbol": {"type": "string", "maxLength": 500},
          "file": {"type": "string", "maxLength": 500},
          "code": {"type": "string", "maxLength": 64},
          "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext&, const nlohmann::json&) -> nlohmann::json {
        throw ToolError("NOT_IMPLEMENTED", "recall_pitfalls is implemented in milestone M6");
      }
  });

  reg.add(Tool{
      .name = "end_session",
      .title = "End session",
      .description = "Call before finishing: store a short summary of what was done and what remains, and mark intents done/abandoned.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "required": ["summary"],
        "properties": {
          "summary": {"type": "string", "minLength": 1, "maxLength": 2000},
          "intents_done": {"type": "array", "items": {"type": "string"}},
          "intents_abandoned": {"type": "array", "items": {"type": "string"}}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext&, const nlohmann::json&) -> nlohmann::json {
        throw ToolError("NOT_IMPLEMENTED", "end_session is implemented in milestone M6");
      }
  });
}

} // namespace gs::mcp
