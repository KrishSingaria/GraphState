#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "index/fact_store.hpp"
#include "memory/session_ctx.hpp"

namespace gs {
class Engine;
}

namespace gs::memory {

struct RememberRequest {
  std::string content;
  std::string kind; // "preference" | "convention" | "fact" | "todo" | "warning"
  std::string scope; // "user" | "project" | "" (default based on kind)
  std::vector<std::string> about;
  std::vector<std::string> tags;
  int importance = 2; // 1..3
  std::string supersedes;
};

struct RememberResult {
  std::string id;
  std::string scope;
  bool deduplicated = false;
  std::vector<std::string> anchors;
};

struct RecallRequest {
  std::string query;
  std::vector<std::string> types; // "memory", "decision", "pitfall", "session", "intent"
  std::string scope = "all"; // "all" | "project" | "user"
  std::string about;
  int limit = 10;
};

struct RecallItem {
  std::string type;
  std::string id;
  double score = 0.0;
  std::string text;
  nlohmann::json meta = nlohmann::json::object();
};

struct RecallResult {
  std::vector<RecallItem> results;
};

struct RecallPitfallsRequest {
  std::string symbol;
  std::string file;
  std::string code;
  int limit = 20;
};

struct PitfallItem {
  std::string key;
  std::string code;
  std::string rule_id;
  std::string symbol_id;
  std::string file;
  std::string lesson;
  int count = 0;
  int resolved_count = 0;
  int64_t first_seen = 0;
  int64_t last_seen = 0;
  int64_t last_resolved = 0;
  bool unresolved = false;
};

struct RecallPitfallsResult {
  std::vector<PitfallItem> pitfalls;
};

class MemoryStore {
public:
  explicit MemoryStore(Engine& engine);
  ~MemoryStore() = default;

  MemoryStore(const MemoryStore&) = delete;
  MemoryStore& operator=(const MemoryStore&) = delete;

  // Remember: stores durable memory with secret check & anchor resolution
  RememberResult remember(const RememberRequest& req, const SessionCtx& session);

  // Recall: searches past memories, decisions, pitfalls, intents, sessions
  RecallResult recall(const RecallRequest& req, const SessionCtx& session);

  // Recall Pitfalls: lists pitfalls recorded from gate rejections
  RecallPitfallsResult recall_pitfalls(const RecallPitfallsRequest& req);

  // Secret guard helper
  static bool contains_secret(std::string_view text);

  // Normalization helpers
  static std::string normalize_content(std::string_view content);
  static std::string sanitize_fulltext_query(std::string_view query);

  // Anchor resolution helper
  std::vector<std::string> resolve_anchors(const std::vector<std::string>& refs, const index::FactStore& store);

private:
  Engine& engine_;
};

} // namespace gs::memory
