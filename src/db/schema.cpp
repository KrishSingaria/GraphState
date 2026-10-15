#include "db/schema.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>
#include "core/clock.hpp"
#include "core/error.hpp"
#include "db/queries.hpp"

#ifndef GS_VERSION
#define GS_VERSION "0.1.0"
#endif

namespace gs::db {

namespace {

std::string to_lower_str(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool is_already_exists_error(const gs::Error& e) {
  std::string msg = to_lower_str(e.what());
  return msg.find("already") != std::string::npos || msg.find("exists") != std::string::npos;
}

void run_index_query_safe(GraphClient& client, const std::string& graph, std::string_view cypher) {
  try {
    client.exec(graph, Query{"schema_index", cypher, nlohmann::json::object(), false});
  } catch (const gs::Error& e) {
    if (!is_already_exists_error(e)) {
      throw;
    }
  }
}

} // namespace

void bootstrap_project_schema(GraphClient& client, const std::string& graph_name) {
  // 1. Range indexes
  static constexpr std::string_view kRangeIndexes[] = {
      "CREATE INDEX FOR (n:File) ON (n.id)",
      "CREATE INDEX FOR (n:Class) ON (n.id)",
      "CREATE INDEX FOR (n:Class) ON (n.file)",
      "CREATE INDEX FOR (n:Function) ON (n.id)",
      "CREATE INDEX FOR (n:Function) ON (n.file)",
      "CREATE INDEX FOR (n:Function) ON (n.name)",
      "CREATE INDEX FOR (n:Session) ON (n.id)",
      "CREATE INDEX FOR (n:Session) ON (n.status)",
      "CREATE INDEX FOR (n:Intent) ON (n.id)",
      "CREATE INDEX FOR (n:Decision) ON (n.patch_id)",
      "CREATE INDEX FOR (n:Decision) ON (n.ts)",
      "CREATE INDEX FOR (n:Pitfall) ON (n.key)",
      "CREATE INDEX FOR (n:Pitfall) ON (n.symbol_id)",
      "CREATE INDEX FOR (n:Pitfall) ON (n.file)",
      "CREATE INDEX FOR (n:Memory) ON (n.id)",
      "CREATE INDEX FOR (n:Meta) ON (n.id)",
      "CREATE INDEX FOR (n:Changeset) ON (n.id)",
      "CREATE INDEX FOR (n:Changeset) ON (n.status)",
      "CREATE INDEX FOR (n:Obligation) ON (n.id)"
  };

  for (auto cypher : kRangeIndexes) {
    run_index_query_safe(client, graph_name, cypher);
  }

  // 2. Fulltext indexes
  static constexpr std::string_view kFulltextIndexes[] = {
      "CREATE FULLTEXT INDEX FOR (n:Function) ON (n.search_text)",
      "CREATE FULLTEXT INDEX FOR (n:Class) ON (n.search_text)",
      "CREATE FULLTEXT INDEX FOR (n:Memory) ON (n.content)",
      "CREATE FULLTEXT INDEX FOR (n:Decision) ON (n.reasoning, n.intent)",
      "CREATE FULLTEXT INDEX FOR (n:Pitfall) ON (n.lesson)",
      "CREATE FULLTEXT INDEX FOR (n:Session) ON (n.summary)",
      "CREATE FULLTEXT INDEX FOR (n:Intent) ON (n.goal)"
  };

  for (auto cypher : kFulltextIndexes) {
    run_index_query_safe(client, graph_name, cypher);
  }

  // 3. Unique node constraints
  struct ConstraintSpec {
    std::string_view label;
    std::string_view prop;
  };

  static constexpr ConstraintSpec kConstraints[] = {
      {"File", "id"},
      {"Class", "id"},
      {"Function", "id"},
      {"Session", "id"},
      {"Intent", "id"},
      {"Decision", "patch_id"},
      {"Pitfall", "key"},
      {"Memory", "id"},
      {"Meta", "id"},
      {"Changeset", "id"}
  };

  for (const auto& c : kConstraints) {
    client.constraint_unique(graph_name, c.label, c.prop);
  }
}

void bootstrap_user_schema(GraphClient& client, const std::string& user_graph_name) {
  run_index_query_safe(client, user_graph_name, "CREATE INDEX FOR (n:Memory) ON (n.id)");
  client.constraint_unique(user_graph_name, "Memory", "id");

  run_index_query_safe(client, user_graph_name, "CREATE INDEX FOR (n:UserProfile) ON (n.id)");
  client.constraint_unique(user_graph_name, "UserProfile", "id");

  run_index_query_safe(client, user_graph_name, "CREATE FULLTEXT INDEX FOR (n:Memory) ON (n.content)");
}

void ensure_meta(GraphClient& client, const std::string& graph_name, const std::string& root, const std::string& workspace_key) {
  nlohmann::json params = {
      {"schema_version", 1},
      {"now", core::now_ms()},
      {"root", root},
      {"key", workspace_key},
      {"engine_version", GS_VERSION}
  };
  client.exec(graph_name, Query{"Q_META_UPSERT", Q_META_UPSERT, params, false});
}

} // namespace gs::db
