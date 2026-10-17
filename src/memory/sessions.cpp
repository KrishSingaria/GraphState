#include "memory/sessions.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include "core/clock.hpp"
#include "core/engine.hpp"
#include "core/hash.hpp"
#include "db/queries.hpp"
#include "platform/platform.hpp"

namespace gs::memory {

namespace {

std::string generate_id(std::string_view prefix) {
  int64_t now = core::now_ms();
  static uint64_t counter = 0;
  ++counter;
  std::string seed = std::to_string(now) + "_" + std::to_string(counter);
  return std::string(prefix) + core::xxh3_hex(seed).substr(0, 12);
}

} // namespace

Sessions::Sessions(Engine& engine) : engine_(engine) {
  ctx_.session_id = generate_id("s_");
}

Sessions::~Sessions() {
  try {
    end_on_shutdown();
  } catch (...) {}
}

std::string Sessions::normalize_goal(std::string_view goal) {
  std::string result;
  result.reserve(goal.size());

  // Lowercase & normalize spaces
  bool in_space = false;
  for (char ch : goal) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!in_space && !result.empty()) {
        result.push_back(' ');
        in_space = true;
      }
    } else {
      result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      in_space = false;
    }
  }

  // Trim trailing whitespace
  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }

  // Strip trailing punctuation
  while (!result.empty() && (result.back() == '.' || result.back() == '!' ||
                             result.back() == '?' || result.back() == ';' ||
                             result.back() == ':')) {
    result.pop_back();
  }

  return result;
}

void Sessions::mark_interrupted_on_startup() {
  if (!engine_.client().ping()) return;

  int64_t cutoff = core::now_ms() - 120'000; // 120 seconds timeout
  try {
    engine_.client().rows(engine_.config().project_graph(), db::Query{
        .name = "sessions_mark_interrupted",
        .cypher = db::Q_SESSIONS_MARK_INTERRUPTED,
        .params = {{"cutoff", cutoff}},
        .read_only = false
    });
  } catch (...) {}
}

const SessionCtx& Sessions::ensure(const nlohmann::json& client_info) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_) {
    if (client_info.is_object()) {
      ctx_.client = client_info.value("name", "unknown");
      ctx_.client_version = client_info.value("version", "unknown");
    }

    if (engine_.client().ping()) {
      int64_t now = core::now_ms();
      try {
        engine_.client().exec(engine_.config().project_graph(), db::Query{
            .name = "session_create",
            .cypher = db::Q_SESSION_CREATE,
            .params = {
                {"id", ctx_.session_id},
                {"client", ctx_.client},
                {"client_version", ctx_.client_version},
                {"now", now}
            },
            .read_only = false
        });

        engine_.client().exec(engine_.config().user_graph(), db::Query{
            .name = "user_profile_upsert",
            .cypher = db::Q_USER_PROFILE_UPSERT,
            .params = {
                {"now", now},
                {"workspace", platform::to_utf8(engine_.config().root)}
            },
            .read_only = false
        });
      } catch (...) {}
    }

    initialized_ = true;
    last_heartbeat_ms_ = core::now_ms();
  }

  return ctx_;
}

void Sessions::heartbeat() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_ || ended_) return;

  int64_t now = core::now_ms();
  if (now - last_heartbeat_ms_ < 30'000) {
    return; // Throttled to at most once per 30s
  }

  last_heartbeat_ms_ = now;
  if (!engine_.client().ping()) return;

  try {
    engine_.client().exec(engine_.config().project_graph(), db::Query{
        .name = "session_heartbeat",
        .cypher = db::Q_SESSION_HEARTBEAT,
        .params = {
            {"id", ctx_.session_id},
            {"now", now}
        },
        .read_only = false
    });
  } catch (...) {}
}

std::string Sessions::upsert_intent(std::string_view goal) {
  std::string norm = normalize_goal(goal);
  if (norm.empty()) return "";

  std::lock_guard<std::mutex> lock(mu_);
  if (!initialized_) {
    // Lazy ensure if not already called
    ensure(nlohmann::json::object());
  }

  int64_t now = core::now_ms();

  if (engine_.client().ping()) {
    try {
      auto found = engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "intent_find",
          .cypher = db::Q_INTENT_FIND,
          .params = {
              {"session", ctx_.session_id},
              {"goal_norm", norm}
          },
          .read_only = true
      });

      if (!found.empty() && found[0].contains("id")) {
        std::string existing_id = found[0]["id"].get<std::string>();
        engine_.client().exec(engine_.config().project_graph(), db::Query{
            .name = "intent_touch",
            .cypher = db::Q_INTENT_TOUCH,
            .params = {
                {"id", existing_id},
                {"now", now}
            },
            .read_only = false
        });
        ctx_.current_intent_id = existing_id;
        return existing_id;
      }
    } catch (...) {}
  }

  // Create new intent
  std::string new_id = generate_id("i_");
  if (engine_.client().ping()) {
    try {
      engine_.client().exec(engine_.config().project_graph(), db::Query{
          .name = "intent_create",
          .cypher = db::Q_INTENT_CREATE,
          .params = {
              {"session", ctx_.session_id},
              {"id", new_id},
              {"goal", std::string(goal)},
              {"goal_norm", norm},
              {"now", now}
          },
          .read_only = false
      });
    } catch (...) {}
  }

  ctx_.current_intent_id = new_id;
  return new_id;
}

void Sessions::end(std::string_view summary,
                   const std::vector<std::string>& done,
                   const std::vector<std::string>& abandoned) {
  std::lock_guard<std::mutex> lock(mu_);
  if (ended_) return;
  ended_ = true;

  if (!initialized_) return;
  if (!engine_.client().ping()) return;

  int64_t now = core::now_ms();
  try {
    if (!done.empty()) {
      engine_.client().exec(engine_.config().project_graph(), db::Query{
          .name = "intents_set_done",
          .cypher = db::Q_INTENTS_SET_STATUS,
          .params = {
              {"ids", done},
              {"status", "DONE"},
              {"now", now}
          },
          .read_only = false
      });
    }

    if (!abandoned.empty()) {
      engine_.client().exec(engine_.config().project_graph(), db::Query{
          .name = "intents_set_abandoned",
          .cypher = db::Q_INTENTS_SET_STATUS,
          .params = {
              {"ids", abandoned},
              {"status", "ABANDONED"},
              {"now", now}
          },
          .read_only = false
      });
    }

    engine_.client().exec(engine_.config().project_graph(), db::Query{
        .name = "session_end",
        .cypher = db::Q_SESSION_END,
        .params = {
            {"id", ctx_.session_id},
            {"summary", std::string(summary)},
            {"now", now}
        },
        .read_only = false
    });
  } catch (...) {}
}

void Sessions::end_on_shutdown() {
  std::string auto_summary;

  {
    std::lock_guard<std::mutex> lock(mu_);
    if (ended_ || !initialized_) return;

    if (engine_.client().ping()) {
      try {
        auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
            .name = "session_auto_summary_data",
            .cypher = db::Q_SESSION_AUTO_SUMMARY_DATA,
            .params = {{"id", ctx_.session_id}},
            .read_only = true
        });

        if (!rows.empty()) {
          const auto& data = rows[0];
          int applied = data.value("applied", 0);
          int rejected = data.value("rejected", 0);

          std::ostringstream ss;
          ss << "Auto-summary: " << applied << " applied / " << rejected << " rejected patches.";

          if (data.contains("intents") && data["intents"].is_array() && !data["intents"].empty()) {
            ss << " Intents: ";
            for (size_t i = 0; i < data["intents"].size() && i < 3; ++i) {
              if (i > 0) ss << "; ";
              ss << data["intents"][i].get<std::string>();
            }
            ss << ".";
          }

          auto_summary = ss.str();
        }
      } catch (...) {}
    }

    if (auto_summary.empty()) {
      auto_summary = "Auto-summary: session ended.";
    }

    if (auto_summary.size() > 600) {
      auto_summary = auto_summary.substr(0, 597) + "...";
    }
  }

  end(auto_summary);
}

bool Sessions::is_active() const {
  std::lock_guard<std::mutex> lock(mu_);
  return initialized_ && !ended_;
}

const SessionCtx& Sessions::ctx() const {
  std::lock_guard<std::mutex> lock(mu_);
  return ctx_;
}

SessionCtx& Sessions::ctx() {
  std::lock_guard<std::mutex> lock(mu_);
  return ctx_;
}

} // namespace gs::memory
