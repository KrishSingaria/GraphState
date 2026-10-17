#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "core/engine.hpp"
#include "gate/gate.hpp"
#include "memory/briefing.hpp"
#include "memory/sessions.hpp"

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
    auto b = memory::generate_briefing("", engine.sessions().ctx(), engine);
    return {
        {"contents", {
            {
                {"uri", "graphstate://briefing"},
                {"mimeType", "text/markdown"},
                {"text", b.markdown}
            }
        }}
    };
  }

  if (uri == "graphstate://rules") {
    nlohmann::json rules = nlohmann::json::array();
    for (const auto& [id, cfg] : engine.gate().rules_engine().rules()) {
      rules.push_back({
          {"id", id},
          {"severity", cfg.severity},
          {"max", cfg.max},
          {"min_lines", cfg.min_lines}
      });
    }
    return {
        {"contents", {
            {
                {"uri", "graphstate://rules"},
                {"mimeType", "application/json"},
                {"text", rules.dump(2)}
            }
        }}
    };
  }

  return nullptr; // Indicates resource not found
}

} // namespace gs::mcp
