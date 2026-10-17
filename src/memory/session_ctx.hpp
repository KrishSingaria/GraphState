#pragma once

#include <string>

namespace gs::memory {

struct SessionCtx {
  std::string session_id;
  std::string current_intent_id;
  std::string client = "unknown";
  std::string client_version = "unknown";
};

} // namespace gs::memory
