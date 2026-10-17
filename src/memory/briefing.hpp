#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "memory/session_ctx.hpp"

namespace gs {
class Engine;
}

namespace gs::memory {

struct BriefingResult {
  std::string markdown;
  nlohmann::json structured_content;
};

BriefingResult generate_briefing(std::string_view goal, const SessionCtx& session, Engine& engine);

} // namespace gs::memory
