#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace gs::mcp {

nlohmann::json handle_prompts_list() {
  return {
      {"prompts", {
          {
              {"name", "graphstate_workflow"},
              {"title", "Work through GraphState"},
              {"description", "Start a task the GraphState way."},
              {"arguments", {
                  {
                      {"name", "task"},
                      {"description", "What you want done"},
                      {"required", true}
                  }
              }}
          }
      }}
  };
}

nlohmann::json handle_prompts_get(std::string_view name, const nlohmann::json& arguments) {
  if (name != "graphstate_workflow") {
    return nullptr;
  }

  std::string task = "general task";
  if (arguments.is_object() && arguments.contains("task") && arguments["task"].is_string()) {
    task = arguments["task"].get<std::string>();
  }

  std::string prompt_text =
      "Task: " + task + "\n\n"
      "Work through GraphState: 1) get_briefing with this task as goal; 2) explore with get_file_outline/search_symbols/read_symbol; "
      "3) get_impact before changing signatures; 4) propose_patch with dry_run first for multi-file changes, then apply; "
      "5) remember any durable preference you learn; 6) end_session with a summary.";

  return {
      {"description", "GraphState workflow"},
      {"messages", {
          {
              {"role", "user"},
              {"content", {
                  {"type", "text"},
                  {"text", prompt_text}
              }}
          }
      }}
  };
}

} // namespace gs::mcp
