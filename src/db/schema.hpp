#pragma once

#include <string>
#include <string_view>
#include "db/graph_client.hpp"

namespace gs::db {

// Bootstraps project schema (range indexes, fulltext indexes, unique constraints).
// Idempotent: can be safely run at every startup.
void bootstrap_project_schema(GraphClient& client, const std::string& graph_name);

// Bootstraps user schema in gs:__user__ (indexes and constraints).
// Idempotent: can be safely run at every startup.
void bootstrap_user_schema(GraphClient& client, const std::string& user_graph_name = "gs:__user__");

// Ensures the (:Meta {id:'meta'}) node is present and up-to-date.
void ensure_meta(GraphClient& client, const std::string& graph_name, const std::string& root, const std::string& workspace_key);

} // namespace gs::db
