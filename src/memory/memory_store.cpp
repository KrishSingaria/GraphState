#include "memory/memory_store.hpp"

#include <algorithm>
#include <cmath>
#include <regex>
#include "core/clock.hpp"
#include "core/engine.hpp"
#include "core/hash.hpp"
#include "core/text.hpp"
#include "db/queries.hpp"
#include "mcp/tool_registry.hpp"
#include "platform/platform.hpp"

namespace gs::memory {

namespace {

const std::vector<std::regex>& secret_regexes() {
  static const std::vector<std::regex> regexes = {
      std::regex(R"(AKIA[0-9A-Z]{16})"),
      std::regex(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----)"),
      std::regex(R"(ghp_[A-Za-z0-9]{36})"),
      std::regex(R"(sk-[A-Za-z0-9_-]{20,})"),
      std::regex(R"(xox[baprs]-[A-Za-z0-9-]{10,})"),
      std::regex(R"((password|secret|token)\s*[:=]\s*\S{8,})", std::regex_constants::icase)
  };
  return regexes;
}

std::string generate_id(std::string_view prefix) {
  int64_t now = core::now_ms();
  static uint64_t counter = 0;
  ++counter;
  std::string seed = std::to_string(now) + "_" + std::to_string(counter);
  return std::string(prefix) + core::xxh3_hex(seed).substr(0, 12);
}

} // namespace

MemoryStore::MemoryStore(Engine& engine) : engine_(engine) {}

bool MemoryStore::contains_secret(std::string_view text) {
  std::string s(text);
  for (const auto& re : secret_regexes()) {
    if (std::regex_search(s, re)) {
      return true;
    }
  }
  return false;
}

std::string MemoryStore::normalize_content(std::string_view content) {
  std::string result;
  result.reserve(content.size());

  bool in_space = false;
  for (char ch : content) {
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

  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }

  return result;
}

std::string MemoryStore::sanitize_fulltext_query(std::string_view query) {
  std::string result;
  result.reserve(query.size());

  bool in_space = false;
  for (char ch : query) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
      result.push_back(ch);
      in_space = false;
    } else {
      if (!in_space && !result.empty()) {
        result.push_back(' ');
        in_space = true;
      }
    }
  }

  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }

  return result;
}

std::vector<std::string> MemoryStore::resolve_anchors(const std::vector<std::string>& refs, const index::FactStore& store) {
  std::vector<std::string> resolved;

  for (const auto& ref : refs) {
    if (ref.empty()) continue;

    // 1. Exact file match in FactStore or on disk
    if (store.contains(ref)) {
      resolved.push_back(ref);
      continue;
    }

    std::error_code ec;
    if (std::filesystem::exists(engine_.config().root / platform::from_utf8(ref), ec)) {
      resolved.push_back(ref);
      continue;
    }

    // 2. Exact symbol ID match
    if (store.find_symbol_by_id(ref)) {
      resolved.push_back(ref);
      continue;
    }

    // 3. Qualname match
    auto q_syms = store.find_symbols_by_qualname(ref);
    if (q_syms.size() == 1) {
      resolved.push_back(q_syms[0]->id);
      continue;
    } else if (q_syms.size() > 1) {
      nlohmann::json candidates = nlohmann::json::array();
      for (size_t i = 0; i < q_syms.size() && i < 10; ++i) {
        candidates.push_back({{"id", q_syms[i]->id}, {"signature", q_syms[i]->sig.canonical()}});
      }
      throw mcp::ToolError("SYMBOL_AMBIGUOUS", "Ambiguous symbol '" + ref + "' matches multiple definitions", {{"candidates", candidates}});
    }

    // 4. Bare name match
    auto n_syms = store.find_symbols_by_name(ref);
    if (n_syms.size() == 1) {
      resolved.push_back(n_syms[0]->id);
      continue;
    } else if (n_syms.size() > 1) {
      nlohmann::json candidates = nlohmann::json::array();
      for (size_t i = 0; i < n_syms.size() && i < 10; ++i) {
        candidates.push_back({{"id", n_syms[i]->id}, {"signature", n_syms[i]->sig.canonical()}});
      }
      throw mcp::ToolError("SYMBOL_AMBIGUOUS", "Ambiguous symbol '" + ref + "' matches multiple definitions", {{"candidates", candidates}});
    }

    // 5. Not found -> find did_you_mean candidates
    std::vector<std::pair<int, std::string>> scored_candidates;
    for (const auto& file : store.files()) {
      int d = static_cast<int>(core::damerau_levenshtein(ref, file));
      if (d <= 3) scored_candidates.push_back({d, file});
    }

    auto all_syms = store.find_symbols(ref, 20);
    for (const auto* s : all_syms) {
      int d = static_cast<int>(core::damerau_levenshtein(ref, s->name));
      if (d <= 3) scored_candidates.push_back({d, s->id});
    }

    std::sort(scored_candidates.begin(), scored_candidates.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });

    nlohmann::json did_you_mean = nlohmann::json::array();
    for (size_t i = 0; i < scored_candidates.size() && i < 5; ++i) {
      did_you_mean.push_back(scored_candidates[i].second);
    }

    throw mcp::ToolError("INVALID_ANCHOR", "Cannot resolve anchor reference '" + ref + "'", {{"did_you_mean", did_you_mean}});
  }

  return resolved;
}

RememberResult MemoryStore::remember(const RememberRequest& req, const SessionCtx& session) {
  // Validate content
  if (req.content.empty() || req.content.size() > 2000) {
    throw mcp::ToolError("INVALID_ARGUMENT", "Memory content must be between 1 and 2000 characters");
  }

  // Secret guard
  if (contains_secret(req.content)) {
    throw mcp::ToolError("SECRET_DETECTED", "Memory content matches secret or credential pattern. Never store credentials in persistent memory.");
  }

  // Resolve scope: preference -> user, others -> project
  std::string scope = req.scope;
  if (scope.empty()) {
    scope = (req.kind == "preference") ? "user" : "project";
  }

  // Resolve anchors
  auto resolved_anchors = resolve_anchors(req.about, engine_.fact_store());

  std::string norm = normalize_content(req.content);
  std::string target_graph = (scope == "user") ? engine_.config().user_graph() : engine_.config().project_graph();

  int64_t now = core::now_ms();

  // Deduplication check
  if (engine_.client().ping()) {
    try {
      auto found = engine_.client().rows(target_graph, db::Query{
          .name = "memory_find_dup",
          .cypher = db::Q_MEMORY_FIND_DUP,
          .params = {
              {"kind", req.kind},
              {"content_norm", norm}
          },
          .read_only = true
      });

      if (!found.empty() && found[0].contains("id")) {
        std::string existing_id = found[0]["id"].get<std::string>();
        engine_.client().exec(target_graph, db::Query{
            .name = "memory_touch",
            .cypher = db::Q_MEMORY_TOUCH,
            .params = {
                {"id", existing_id},
                {"importance", req.importance},
                {"tags", req.tags},
                {"now", now}
            },
            .read_only = false
        });

        return RememberResult{
            .id = existing_id,
            .scope = scope,
            .deduplicated = true,
            .anchors = resolved_anchors
        };
      }
    } catch (...) {}
  }

  // Create new memory
  std::string mem_id = generate_id("m_");
  std::string source_session = (scope == "user")
      ? (engine_.config().workspace_key() + "/" + session.session_id)
      : session.session_id;

  nlohmann::json m_payload = {
      {"id", mem_id},
      {"kind", req.kind},
      {"scope", scope},
      {"content", req.content},
      {"content_norm", norm},
      {"tags", req.tags},
      {"anchors", resolved_anchors},
      {"importance", req.importance},
      {"source_session", source_session}
  };

  if (engine_.client().ping()) {
    try {
      engine_.client().exec(target_graph, db::Query{
          .name = "memory_create",
          .cypher = db::Q_MEMORY_CREATE,
          .params = {
              {"m", m_payload},
              {"now", now}
          },
          .read_only = false
      });

      if (scope == "project") {
        engine_.client().exec(target_graph, db::Query{
            .name = "memory_link",
            .cypher = db::Q_MEMORY_LINK,
            .params = {
                {"id", mem_id},
                {"session", session.session_id},
                {"anchors", resolved_anchors}
            },
            .read_only = false
        });
      }

      // Supersedes handling
      if (!req.supersedes.empty()) {
        try {
          engine_.client().exec(engine_.config().project_graph(), db::Query{
              .name = "memory_supersede",
              .cypher = db::Q_MEMORY_SUPERSEDE,
              .params = {
                  {"old_id", req.supersedes},
                  {"new_id", mem_id},
                  {"now", now}
              },
              .read_only = false
          });
        } catch (...) {
          // Try user graph if project graph failed
          engine_.client().exec(engine_.config().user_graph(), db::Query{
              .name = "memory_supersede_user",
              .cypher = db::Q_MEMORY_SUPERSEDE,
              .params = {
                  {"old_id", req.supersedes},
                  {"new_id", mem_id},
                  {"now", now}
              },
              .read_only = false
          });
        }
      }
    } catch (...) {}
  }

  return RememberResult{
      .id = mem_id,
      .scope = scope,
      .deduplicated = false,
      .anchors = resolved_anchors
  };
}

RecallResult MemoryStore::recall(const RecallRequest& req, const SessionCtx& session) {
  (void)session;
  if (req.query.empty() && req.about.empty()) {
    throw mcp::ToolError("EMPTY_QUERY", "Either 'query' or 'about' must be provided for recall");
  }

  std::vector<std::string> types = req.types;
  if (types.empty()) {
    types = {"memory", "decision", "pitfall", "session", "intent"};
  }

  std::string q_clean = sanitize_fulltext_query(req.query);
  int64_t now = core::now_ms();

  struct ScoredRawItem {
    RecallItem item;
    double raw_score = 0.0;
    int64_t ts = 0;
    bool is_anchored = false;
  };

  std::vector<ScoredRawItem> raw_items;

  // 1. Check anchor recall if 'about' specified
  if (!req.about.empty()) {
    std::string anchor_id = req.about;
    try {
      auto resolved = resolve_anchors({req.about}, engine_.fact_store());
      if (!resolved.empty()) anchor_id = resolved[0];
    } catch (...) {}

    if (engine_.client().ping()) {
      try {
        auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
            .name = "recall_by_anchor",
            .cypher = db::Q_RECALL_BY_ANCHOR,
            .params = {{"id", anchor_id}, {"limit", req.limit}},
            .read_only = true
        });

        for (const auto& r : rows) {
          ScoredRawItem s;
          s.item.type = "memory";
          s.item.id = r.value("id", "");
          s.item.text = r.value("content", "");
          if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
          s.item.meta = {
              {"kind", r.value("kind", "")},
              {"scope", r.value("scope", "")}
          };
          s.raw_score = 100.0;
          s.item.score = 100.0;
          s.ts = r.value("updated_at", now);
          s.is_anchored = true;
          raw_items.push_back(std::move(s));
        }
      } catch (...) {}
    }
  }

  // 2. Fulltext query across requested types
  if (!q_clean.empty() && engine_.client().ping()) {
    for (const auto& type : types) {
      if (type == "memory") {
        if (req.scope == "all" || req.scope == "project") {
          try {
            auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
                .name = "recall_memory_project",
                .cypher = db::Q_RECALL_MEMORY,
                .params = {{"q", q_clean}, {"limit", req.limit}},
                .read_only = true
            });
            for (const auto& r : rows) {
              ScoredRawItem s;
              s.item.type = "memory";
              s.item.id = r.value("id", "");
              s.item.text = r.value("content", "");
              if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
              s.item.meta = {
                  {"kind", r.value("kind", "")},
                  {"scope", r.value("scope", "project")}
              };
              s.raw_score = r.value("score", 1.0);
              s.ts = r.value("updated_at", now);
              raw_items.push_back(std::move(s));
            }
          } catch (...) {}
        }

        if (req.scope == "all" || req.scope == "user") {
          try {
            auto rows = engine_.client().rows(engine_.config().user_graph(), db::Query{
                .name = "recall_memory_user",
                .cypher = db::Q_RECALL_MEMORY,
                .params = {{"q", q_clean}, {"limit", req.limit}},
                .read_only = true
            });
            for (const auto& r : rows) {
              ScoredRawItem s;
              s.item.type = "memory";
              s.item.id = r.value("id", "");
              s.item.text = r.value("content", "");
              if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
              s.item.meta = {
                  {"kind", r.value("kind", "")},
                  {"scope", r.value("scope", "user")}
              };
              s.raw_score = r.value("score", 1.0);
              s.ts = r.value("updated_at", now);
              raw_items.push_back(std::move(s));
            }
          } catch (...) {}
        }
      } else if (type == "decision") {
        try {
          auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
              .name = "recall_decisions",
              .cypher = db::Q_RECALL_DECISIONS,
              .params = {{"q", q_clean}, {"limit", req.limit}},
              .read_only = true
          });
          for (const auto& r : rows) {
            ScoredRawItem s;
            s.item.type = "decision";
            s.item.id = r.value("id", "");
            std::string st = r.value("status", "");
            std::string it = r.value("intent", "");
            std::string re = r.value("reasoning", "");
            s.item.text = "[" + st + "] " + it + " — " + re;
            if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
            s.item.meta = {{"ts", r.value("ts", now)}};
            s.raw_score = r.value("score", 1.0);
            s.ts = r.value("ts", now);
            raw_items.push_back(std::move(s));
          }
        } catch (...) {}
      } else if (type == "pitfall") {
        try {
          auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
              .name = "recall_pitfalls",
              .cypher = db::Q_RECALL_PITFALLS,
              .params = {{"q", q_clean}, {"limit", req.limit}},
              .read_only = true
          });
          for (const auto& r : rows) {
            ScoredRawItem s;
            s.item.type = "pitfall";
            s.item.id = r.value("id", "");
            s.item.text = r.value("lesson", "");
            if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
            s.item.meta = {
                {"code", r.value("code", "")},
                {"symbol_id", r.value("symbol_id", "")},
                {"count", r.value("count", 1)}
            };
            s.raw_score = r.value("score", 1.0);
            s.ts = now;
            raw_items.push_back(std::move(s));
          }
        } catch (...) {}
      } else if (type == "session") {
        try {
          auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
              .name = "recall_sessions",
              .cypher = db::Q_RECALL_SESSIONS,
              .params = {{"q", q_clean}, {"limit", req.limit}},
              .read_only = true
          });
          for (const auto& r : rows) {
            ScoredRawItem s;
            s.item.type = "session";
            s.item.id = r.value("id", "");
            s.item.text = r.value("summary", "");
            if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
            s.item.meta = {{"status", r.value("status", "")}};
            s.raw_score = r.value("score", 1.0);
            s.ts = r.value("started_at", now);
            raw_items.push_back(std::move(s));
          }
        } catch (...) {}
      } else if (type == "intent") {
        try {
          auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
              .name = "recall_intents",
              .cypher = db::Q_RECALL_INTENTS,
              .params = {{"q", q_clean}, {"limit", req.limit}},
              .read_only = true
          });
          for (const auto& r : rows) {
            ScoredRawItem s;
            s.item.type = "intent";
            s.item.id = r.value("id", "");
            s.item.text = r.value("goal", "");
            if (s.item.text.size() > 300) s.item.text = s.item.text.substr(0, 297) + "...";
            s.item.meta = {{"status", r.value("status", "")}};
            s.raw_score = r.value("score", 1.0);
            s.ts = r.value("updated_at", now);
            raw_items.push_back(std::move(s));
          }
        } catch (...) {}
      }
    }
  }

  // 3. Normalization, type weighting, and recency boost
  std::map<std::string, double> max_score_by_type;
  for (const auto& s : raw_items) {
    if (!s.is_anchored) {
      max_score_by_type[s.item.type] = std::max(max_score_by_type[s.item.type], s.raw_score);
    }
  }

  for (auto& s : raw_items) {
    if (s.is_anchored) {
      s.item.score = 100.0;
      continue;
    }

    double max_s = max_score_by_type[s.item.type];
    double norm_score = (max_s > 0.0) ? (s.raw_score / max_s) : 1.0;

    double type_weight = 1.0;
    if (s.item.type == "memory") type_weight = 1.0;
    else if (s.item.type == "pitfall") type_weight = 0.9;
    else if (s.item.type == "decision") type_weight = 0.8;
    else if (s.item.type == "intent") type_weight = 0.6;
    else if (s.item.type == "session") type_weight = 0.5;

    double age_days = (s.ts > 0 && now >= s.ts) ? ((now - s.ts) / 86'400'000.0) : 0.0;
    double recency_boost = 1.0 + 0.25 * std::exp(-std::max(0.0, age_days) / 14.0);

    s.item.score = norm_score * type_weight * recency_boost;
  }

  // Sort descending by score
  std::sort(raw_items.begin(), raw_items.end(), [](const auto& a, const auto& b) {
    return a.item.score > b.item.score;
  });

  RecallResult res;
  for (size_t i = 0; i < raw_items.size() && static_cast<int>(i) < req.limit; ++i) {
    res.results.push_back(std::move(raw_items[i].item));
  }

  return res;
}

RecallPitfallsResult MemoryStore::recall_pitfalls(const RecallPitfallsRequest& req) {
  RecallPitfallsResult result;
  if (!engine_.client().ping()) return result;

  try {
    auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
        .name = "pitfalls_list",
        .cypher = db::Q_PITFALLS_LIST,
        .params = {
            {"symbol", req.symbol},
            {"file", req.file},
            {"code", req.code},
            {"limit", req.limit}
        },
        .read_only = true
    });

    for (const auto& r : rows) {
      PitfallItem p;
      p.key = r.value("key", "");
      p.code = r.value("code", "");
      p.rule_id = r.value("rule_id", "");
      p.symbol_id = r.value("symbol_id", "");
      p.file = r.value("file", "");
      p.lesson = r.value("lesson", "");
      p.count = r.value("count", 0);
      p.resolved_count = r.value("resolved_count", 0);
      p.first_seen = r.value("first_seen", 0LL);
      p.last_seen = r.value("last_seen", 0LL);
      p.last_resolved = r.value("last_resolved", 0LL);
      p.unresolved = (p.last_resolved < p.last_seen);
      result.pitfalls.push_back(std::move(p));
    }
  } catch (...) {}

  return result;
}

} // namespace gs::memory
