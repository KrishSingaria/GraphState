#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "memory/session_ctx.hpp"

namespace gs {
class Engine;
}

namespace gs::memory {

class Sessions {
public:
  explicit Sessions(Engine& engine);
  ~Sessions();

  Sessions(const Sessions&) = delete;
  Sessions& operator=(const Sessions&) = delete;

  // Called on engine initialize to mark abandoned sessions as INTERRUPTED
  void mark_interrupted_on_startup();

  // Idempotent: ensures session exists in FalkorDB and creates UserProfile in user graph
  const SessionCtx& ensure(const nlohmann::json& client_info);

  // Throttled heartbeat (at most once per 30s)
  void heartbeat();

  // Deduplicates goal within session; returns intent id and sets current_intent_id
  std::string upsert_intent(std::string_view goal);

  // Ends session explicitly with summary and intent status updates
  void end(std::string_view summary,
           const std::vector<std::string>& done = {},
           const std::vector<std::string>& abandoned = {});

  // Ends session on server shutdown (auto-summary if not ended)
  void end_on_shutdown();

  bool is_active() const;

  const SessionCtx& ctx() const;
  SessionCtx& ctx();

  // Goal normalization helper (lowercase, trim, collapse whitespace, strip trailing punctuation)
  static std::string normalize_goal(std::string_view goal);

private:
  Engine& engine_;
  mutable std::mutex mu_;
  SessionCtx ctx_;
  bool initialized_{false};
  bool ended_{false};
  int64_t last_heartbeat_ms_{0};
};

} // namespace gs::memory
