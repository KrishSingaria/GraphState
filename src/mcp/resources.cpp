#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "core/engine.hpp"

namespace gs::mcp {

nlohmann::json handle_resources_list() {
  return {
      {"resources", {
          {
              {"uri", "graphstate://briefing"},
              {"name", "briefing"},
              {"title", "Session briefing"},
              {"description", "Same as get_briefing (no goal)."},
              {"mimeType", "text/markdown"}
          },
          {
              {"uri", "graphstate://rules"},
              {"name", "rules"},
              {"title", "Effective quality rules"},
              {"description", "Built-in + rules.json, with severities."},
              {"mimeType", "application/json"}
          }
      }}
  };
}

nlohmann::json handle_resources_read(Engine& engine, std::string_view uri) {
  if (uri == "graphstate://briefing") {
    std::string text = "# GraphState briefing\n\nWorkspace: " + engine.config().workspace_key() + "\n";
    return {
        {"contents", {
            {
                {"uri", "graphstate://briefing"},
                {"mimeType", "text/markdown"},
                {"text", text}
            }
        }}
    };
  }

  if (uri == "graphstate://rules") {
    nlohmann::json rules = nlohmann::json::array();
    return {
        {"contents", {
            {
                {"uri", "graphstate://rules"},
                {"mimeType", "application/json"},
                {"text", rules.dump()}
            }
        }}
    };
  }

  return nullptr; // Indicates resource not found
}

} // namespace gs::mcp
