#include "db/result.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include "core/error.hpp"

namespace gs::db {

namespace {

bool starts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

void parse_stat_line(std::string_view line, QueryStats& stats) {
  constexpr std::string_view kNodesCreated = "Nodes created: ";
  constexpr std::string_view kNodesDeleted = "Nodes deleted: ";
  constexpr std::string_view kRelsCreated = "Relationships created: ";
  constexpr std::string_view kRelsDeleted = "Relationships deleted: ";
  constexpr std::string_view kPropsSet = "Properties set: ";
  constexpr std::string_view kCached = "Cached execution: ";
  constexpr std::string_view kExecTime = "Query internal execution time: ";

  if (starts_with(line, kNodesCreated)) {
    stats.nodes_created = std::atoi(line.data() + kNodesCreated.size());
  } else if (starts_with(line, kNodesDeleted)) {
    stats.nodes_deleted = std::atoi(line.data() + kNodesDeleted.size());
  } else if (starts_with(line, kRelsCreated)) {
    stats.rels_created = std::atoi(line.data() + kRelsCreated.size());
  } else if (starts_with(line, kRelsDeleted)) {
    stats.rels_deleted = std::atoi(line.data() + kRelsDeleted.size());
  } else if (starts_with(line, kPropsSet)) {
    stats.props_set = std::atoi(line.data() + kPropsSet.size());
  } else if (starts_with(line, kCached)) {
    int val = std::atoi(line.data() + kCached.size());
    stats.cached = (val == 1);
  } else if (starts_with(line, kExecTime)) {
    stats.exec_ms = std::strtod(line.data() + kExecTime.size(), nullptr);
  }
}

bool is_stats_array(const redisReply* r) {
  if (!r || r->type != REDIS_REPLY_ARRAY || r->elements == 0) {
    return false;
  }
  for (size_t i = 0; i < r->elements; ++i) {
    const redisReply* item = r->element[i];
    if (item && item->type == REDIS_REPLY_STRING && item->str) {
      std::string_view s(item->str, item->len);
      if (starts_with(s, "Query internal execution time:") ||
          starts_with(s, "Cached execution:") ||
          starts_with(s, "Nodes created:") ||
          starts_with(s, "Nodes deleted:") ||
          starts_with(s, "Relationships created:") ||
          starts_with(s, "Relationships deleted:") ||
          starts_with(s, "Properties set:") ||
          starts_with(s, "Labels added:")) {
        return true;
      }
    }
  }
  return false;
}

void extract_stats(const redisReply* stats_arr, QueryStats& stats) {
  if (!stats_arr || stats_arr->type != REDIS_REPLY_ARRAY) {
    return;
  }
  for (size_t i = 0; i < stats_arr->elements; ++i) {
    const redisReply* item = stats_arr->element[i];
    if (item && item->type == REDIS_REPLY_STRING && item->str) {
      parse_stat_line(std::string_view(item->str, item->len), stats);
    }
  }
}

} // namespace

std::vector<nlohmann::json> parse_rows(const redisReply* reply, std::string_view query_name) {
  if (!reply) {
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Null redis reply", {{"query_name", query_name}});
  }
  if (reply->type == REDIS_REPLY_ERROR) {
    std::string err_msg = reply->str ? reply->str : "unknown Redis error";
    throw gs::Error(ErrorCode::DB_QUERY, err_msg, {{"query_name", query_name}});
  }

  std::vector<nlohmann::json> results;
  if (reply->type != REDIS_REPLY_ARRAY) {
    return results;
  }

  const redisReply* rows_arr = nullptr;
  if (reply->elements == 3) {
    // Standard read response: [header, rows, stats]
    rows_arr = reply->element[1];
  } else if (reply->elements == 2) {
    // Either [header, stats] with no rows, or [rows, stats]
    if (is_stats_array(reply->element[1])) {
      if (!is_stats_array(reply->element[0])) {
        // element[0] could be rows
        rows_arr = reply->element[0];
      }
    }
  } else if (reply->elements == 1) {
    // Write query: [stats]
    return results;
  }

  if (!rows_arr || rows_arr->type != REDIS_REPLY_ARRAY) {
    return results;
  }

  results.reserve(rows_arr->elements);
  for (size_t i = 0; i < rows_arr->elements; ++i) {
    const redisReply* row = rows_arr->element[i];
    if (!row) {
      continue;
    }

    if (row->type == REDIS_REPLY_ARRAY) {
      if (row->elements >= 1) {
        const redisReply* col = row->element[0];
        if (col && col->type == REDIS_REPLY_STRING && col->str) {
          try {
            results.push_back(nlohmann::json::parse(std::string_view(col->str, col->len)));
          } catch (const nlohmann::json::parse_error& e) {
            throw gs::Error(ErrorCode::DB_QUERY,
                            std::string("Failed to parse JSON column 'j': ") + e.what(),
                            {{"query_name", query_name}, {"raw", std::string(col->str, col->len)}});
          }
        } else if (col && col->type == REDIS_REPLY_NIL) {
          results.push_back(nullptr);
        } else if (col && col->type == REDIS_REPLY_INTEGER) {
          results.push_back(col->integer);
        }
      }
    } else if (row->type == REDIS_REPLY_STRING && row->str) {
      try {
        results.push_back(nlohmann::json::parse(std::string_view(row->str, row->len)));
      } catch (const nlohmann::json::parse_error& e) {
        throw gs::Error(ErrorCode::DB_QUERY,
                        std::string("Failed to parse JSON row: ") + e.what(),
                        {{"query_name", query_name}, {"raw", std::string(row->str, row->len)}});
      }
    }
  }

  return results;
}

QueryStats parse_stats(const redisReply* reply, std::string_view query_name) {
  if (!reply) {
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Null redis reply", {{"query_name", query_name}});
  }
  if (reply->type == REDIS_REPLY_ERROR) {
    std::string err_msg = reply->str ? reply->str : "unknown Redis error";
    throw gs::Error(ErrorCode::DB_QUERY, err_msg, {{"query_name", query_name}});
  }

  QueryStats stats;
  if (reply->type != REDIS_REPLY_ARRAY) {
    return stats;
  }

  if (reply->elements == 3) {
    extract_stats(reply->element[2], stats);
  } else if (reply->elements == 1) {
    extract_stats(reply->element[0], stats);
  } else if (reply->elements == 2) {
    if (is_stats_array(reply->element[1])) {
      extract_stats(reply->element[1], stats);
    } else if (is_stats_array(reply->element[0])) {
      extract_stats(reply->element[0], stats);
    }
  }

  return stats;
}

} // namespace gs::db
