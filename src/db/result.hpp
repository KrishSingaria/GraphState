#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include "core/error.hpp"

namespace gs::db {

struct QueryStats {
  int nodes_created = 0;
  int nodes_deleted = 0;
  int rels_created = 0;
  int rels_deleted = 0;
  int props_set = 0;
  bool cached = false;
  double exec_ms = 0.0;
};

// Parses FalkorDB verbose query reply:
// Convention: every read query returns exactly one column named 'j' whose value is toJSON(...).
// Extracts and parses column 'j' of every row into a vector of nlohmann::json.
std::vector<nlohmann::json> parse_rows(const redisReply* reply, std::string_view query_name = "");

// Parses FalkorDB verbose query reply for stats lines into QueryStats.
QueryStats parse_stats(const redisReply* reply, std::string_view query_name = "");

} // namespace gs::db
