#include "db/graph_client.hpp"

#ifndef _WIN32
#include <sys/time.h>
#else
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
  long tv_sec;
  long tv_usec;
};
#endif
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include "core/clock.hpp"
#include "core/error.hpp"
#include "db/cypher_params.hpp"
#include "db/result.hpp"

namespace gs::db {

namespace {

std::string str_to_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
  std::string h = str_to_lower(haystack);
  std::string n = str_to_lower(needle);
  return h.find(n) != std::string::npos;
}

nlohmann::json reply_to_json(const redisReply* r) {
  if (!r) {
    return nullptr;
  }
  switch (r->type) {
    case REDIS_REPLY_STRING:
      return std::string(r->str, r->len);
    case REDIS_REPLY_ARRAY: {
      nlohmann::json arr = nlohmann::json::array();
      for (size_t i = 0; i < r->elements; ++i) {
        arr.push_back(reply_to_json(r->element[i]));
      }
      return arr;
    }
    case REDIS_REPLY_INTEGER:
      return r->integer;
    case REDIS_REPLY_NIL:
      return nullptr;
    case REDIS_REPLY_STATUS:
      return std::string(r->str, r->len);
    case REDIS_REPLY_ERROR:
      return nlohmann::json{{"error", std::string(r->str, r->len)}};
    default:
      return std::string(r->str ? r->str : "");
  }
}

} // namespace

GraphClient::GraphClient(std::string host, int port, std::string password, int connect_timeout_ms, int query_timeout_ms)
    : host_(std::move(host)),
      port_(port),
      password_(std::move(password)),
      connect_timeout_ms_(connect_timeout_ms),
      query_timeout_ms_(query_timeout_ms) {
  try {
    connect();
  } catch (const gs::Error&) {
    // Disconnected state; ensure_connected() will connect or throw on query execution
  }
}

GraphClient::~GraphClient() {
  disconnect();
}

GraphClient::GraphClient(GraphClient&& other) noexcept
    : host_(std::move(other.host_)),
      port_(other.port_),
      password_(std::move(other.password_)),
      connect_timeout_ms_(other.connect_timeout_ms_),
      query_timeout_ms_(other.query_timeout_ms_),
      ctx_(other.ctx_),
      is_alive_cached_(other.is_alive_cached_),
      last_check_ms_(other.last_check_ms_),
      backoff_interval_ms_(other.backoff_interval_ms_) {
  other.ctx_ = nullptr;
}

GraphClient& GraphClient::operator=(GraphClient&& other) noexcept {
  if (this != &other) {
    disconnect();
    host_ = std::move(other.host_);
    port_ = other.port_;
    password_ = std::move(other.password_);
    connect_timeout_ms_ = other.connect_timeout_ms_;
    query_timeout_ms_ = other.query_timeout_ms_;
    ctx_ = other.ctx_;
    other.ctx_ = nullptr;
    is_alive_cached_ = other.is_alive_cached_;
    last_check_ms_ = other.last_check_ms_;
    backoff_interval_ms_ = other.backoff_interval_ms_;
  }
  return *this;
}

void GraphClient::connect() {
  disconnect();

  struct timeval tv;
  tv.tv_sec = static_cast<long>(connect_timeout_ms_ / 1000);
  tv.tv_usec = static_cast<long>((connect_timeout_ms_ % 1000) * 1000);

  ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);
  if (!ctx_ || ctx_->err) {
    is_alive_cached_ = false;
    last_check_ms_ = core::now_ms();
    std::string err = ctx_ ? ctx_->errstr : "unable to allocate redis context";
    disconnect();
    throw gs::Error(ErrorCode::DB_UNAVAILABLE,
                    "FalkorDB connection failed to " + host_ + ":" + std::to_string(port_) + ": " + err);
  }
  is_alive_cached_ = true;

  struct timeval qtv;
  qtv.tv_sec = static_cast<long>((query_timeout_ms_ + 1000) / 1000);
  qtv.tv_usec = static_cast<long>(((query_timeout_ms_ + 1000) % 1000) * 1000);
  redisSetTimeout(ctx_, qtv);

  if (!password_.empty()) {
    void* r = redisCommand(ctx_, "AUTH %s", password_.c_str());
    if (!r) {
      disconnect();
      throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Redis AUTH failed: no reply");
    }
    redisReply* reply = static_cast<redisReply*>(r);
    if (reply->type == REDIS_REPLY_ERROR) {
      std::string err = reply->str ? reply->str : "unknown error";
      freeReplyObject(reply);
      disconnect();
      throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Redis AUTH error: " + err);
    }
    freeReplyObject(reply);
  }
}

void GraphClient::disconnect() {
  if (ctx_) {
    redisFree(ctx_);
    ctx_ = nullptr;
  }
}

void GraphClient::ensure_connected() {
  if (!ctx_ || ctx_->err) {
    connect();
  }
}

redisReply* GraphClient::execute_command(const std::vector<std::string>& argv, bool read_only, std::string_view query_name) {
  ensure_connected();

  std::vector<const char*> argv_ptrs;
  std::vector<size_t> argv_lens;
  argv_ptrs.reserve(argv.size());
  argv_lens.reserve(argv.size());
  for (const auto& a : argv) {
    argv_ptrs.push_back(a.data());
    argv_lens.push_back(a.size());
  }

  void* r = redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv_ptrs.data(), argv_lens.data());

  if (!r) {
    if (ctx_->err == REDIS_ERR_IO || ctx_->err == REDIS_ERR_EOF) {
      if (read_only) {
        // Retry once for read-only query
        try {
          connect();
          r = redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv_ptrs.data(), argv_lens.data());
        } catch (...) {
          r = nullptr;
        }
      }
    }
  }

  if (!r) {
    std::string err = (ctx_ && ctx_->errstr[0] != '\0') ? ctx_->errstr : "null redis reply";
    disconnect();
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Redis command failed: " + err, {{"query_name", query_name}});
  }

  redisReply* reply = static_cast<redisReply*>(r);
  if (reply->type == REDIS_REPLY_ERROR) {
    std::string err_msg = reply->str ? reply->str : "unknown error";
    freeReplyObject(reply);
    throw gs::Error(ErrorCode::DB_QUERY, err_msg, {{"query_name", query_name}});
  }

  return reply;
}

bool GraphClient::ping(bool force) {
  int64_t now = core::now_ms();
  if (!force && !is_alive_cached_ && (now - last_check_ms_ < backoff_interval_ms_)) {
    return false;
  }

  last_check_ms_ = now;
  try {
    ensure_connected();
    void* r = redisCommand(ctx_, "PING");
    if (!r) {
      is_alive_cached_ = false;
      return false;
    }
    redisReply* reply = static_cast<redisReply*>(r);
    bool ok = (reply->type == REDIS_REPLY_STATUS && reply->str && std::string_view(reply->str) == "PONG");
    freeReplyObject(reply);
    is_alive_cached_ = ok;
    return ok;
  } catch (...) {
    is_alive_cached_ = false;
    return false;
  }
}

void GraphClient::reset_health() {
  is_alive_cached_ = true;
  last_check_ms_ = 0;
}

std::vector<nlohmann::json> GraphClient::rows(const std::string& graph, const Query& q) {
  std::string prefix = encode_params(q.params);
  std::string full_cypher = prefix.empty() ? std::string(q.cypher) : prefix + " " + std::string(q.cypher);

  std::vector<std::string> argv = {
      std::string(q.read_only ? "GRAPH.RO_QUERY" : "GRAPH.QUERY"),
      graph,
      full_cypher,
      "TIMEOUT",
      std::to_string(query_timeout_ms_)
  };

  redisReply* reply = execute_command(argv, q.read_only, q.name);
  try {
    auto res = parse_rows(reply, q.name);
    freeReplyObject(reply);
    return res;
  } catch (...) {
    freeReplyObject(reply);
    throw;
  }
}

nlohmann::json GraphClient::one(const std::string& graph, const Query& q) {
  auto r = rows(graph, q);
  if (r.empty()) {
    return nullptr;
  }
  return r[0];
}

QueryStats GraphClient::exec(const std::string& graph, const Query& q) {
  std::string prefix = encode_params(q.params);
  std::string full_cypher = prefix.empty() ? std::string(q.cypher) : prefix + " " + std::string(q.cypher);

  std::vector<std::string> argv = {
      std::string(q.read_only ? "GRAPH.RO_QUERY" : "GRAPH.QUERY"),
      graph,
      full_cypher,
      "TIMEOUT",
      std::to_string(query_timeout_ms_)
  };

  redisReply* reply = execute_command(argv, q.read_only, q.name);
  try {
    auto stats = parse_stats(reply, q.name);
    freeReplyObject(reply);
    return stats;
  } catch (...) {
    freeReplyObject(reply);
    throw;
  }
}

std::vector<nlohmann::json> GraphClient::pipeline(const std::string& graph, const std::vector<Query>& qs) {
  if (qs.empty()) {
    return {};
  }
  ensure_connected();

  for (const auto& q : qs) {
    std::string prefix = encode_params(q.params);
    std::string full_cypher = prefix.empty() ? std::string(q.cypher) : prefix + " " + std::string(q.cypher);
    std::vector<std::string> argv = {
        std::string(q.read_only ? "GRAPH.RO_QUERY" : "GRAPH.QUERY"),
        graph,
        full_cypher,
        "TIMEOUT",
        std::to_string(query_timeout_ms_)
    };
    std::vector<const char*> argv_ptrs;
    std::vector<size_t> argv_lens;
    argv_ptrs.reserve(argv.size());
    argv_lens.reserve(argv.size());
    for (const auto& a : argv) {
      argv_ptrs.push_back(a.data());
      argv_lens.push_back(a.size());
    }
    if (redisAppendCommandArgv(ctx_, static_cast<int>(argv.size()), argv_ptrs.data(), argv_lens.data()) != REDIS_OK) {
      disconnect();
      throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Failed to append command to pipeline");
    }
  }

  std::vector<nlohmann::json> results;
  results.reserve(qs.size());
  std::string first_error;
  std::string first_error_query;

  for (size_t i = 0; i < qs.size(); ++i) {
    void* r = nullptr;
    if (redisGetReply(ctx_, &r) != REDIS_OK || !r) {
      if (first_error.empty()) {
        first_error = (ctx_ && ctx_->errstr[0] != '\0') ? ctx_->errstr : "error reading pipeline reply";
        first_error_query = qs[i].name;
      }
      continue;
    }
    redisReply* reply = static_cast<redisReply*>(r);
    if (reply->type == REDIS_REPLY_ERROR) {
      if (first_error.empty()) {
        first_error = reply->str ? reply->str : "pipeline error";
        first_error_query = qs[i].name;
      }
    } else if (first_error.empty()) {
      try {
        if (qs[i].read_only) {
          results.push_back(parse_rows(reply, qs[i].name));
        } else {
          QueryStats st = parse_stats(reply, qs[i].name);
          results.push_back({
              {"nodes_created", st.nodes_created},
              {"nodes_deleted", st.nodes_deleted},
              {"rels_created", st.rels_created},
              {"rels_deleted", st.rels_deleted},
              {"props_set", st.props_set},
              {"cached", st.cached},
              {"exec_ms", st.exec_ms}
          });
        }
      } catch (const gs::Error& e) {
        if (first_error.empty()) {
          first_error = e.what();
          first_error_query = qs[i].name;
        }
      }
    }
    freeReplyObject(reply);
  }

  if (!first_error.empty()) {
    throw gs::Error(ErrorCode::DB_QUERY, first_error, {{"query_name", first_error_query}});
  }

  return results;
}

void GraphClient::transaction(const std::string& graph, const std::vector<Query>& qs) {
  if (qs.empty()) {
    return;
  }
  ensure_connected();

  void* r_multi = redisCommand(ctx_, "MULTI");
  if (!r_multi) {
    disconnect();
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Failed to start transaction (MULTI)");
  }
  freeReplyObject(r_multi);

  for (const auto& q : qs) {
    std::string prefix = encode_params(q.params);
    std::string full_cypher = prefix.empty() ? std::string(q.cypher) : prefix + " " + std::string(q.cypher);
    std::vector<std::string> argv = {
        "GRAPH.QUERY",
        graph,
        full_cypher,
        "TIMEOUT",
        std::to_string(query_timeout_ms_)
    };
    std::vector<const char*> argv_ptrs;
    std::vector<size_t> argv_lens;
    argv_ptrs.reserve(argv.size());
    argv_lens.reserve(argv.size());
    for (const auto& a : argv) {
      argv_ptrs.push_back(a.data());
      argv_lens.push_back(a.size());
    }
    if (redisAppendCommandArgv(ctx_, static_cast<int>(argv.size()), argv_ptrs.data(), argv_lens.data()) != REDIS_OK) {
      disconnect();
      throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Failed to append command in transaction");
    }
  }

  // Drain the QUEUED replies
  for (size_t i = 0; i < qs.size(); ++i) {
    void* r = nullptr;
    if (redisGetReply(ctx_, &r) != REDIS_OK || !r) {
      disconnect();
      throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Error queuing command in transaction");
    }
    freeReplyObject(r);
  }

  void* r_exec = redisCommand(ctx_, "EXEC");
  if (!r_exec) {
    disconnect();
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Failed to execute transaction (EXEC)");
  }
  redisReply* exec_reply = static_cast<redisReply*>(r_exec);
  if (exec_reply->type == REDIS_REPLY_ERROR) {
    std::string err = exec_reply->str ? exec_reply->str : "EXEC error";
    freeReplyObject(exec_reply);
    throw gs::Error(ErrorCode::DB_QUERY, err);
  }

  if (exec_reply->type == REDIS_REPLY_ARRAY) {
    for (size_t i = 0; i < exec_reply->elements; ++i) {
      redisReply* sub = exec_reply->element[i];
      if (sub && sub->type == REDIS_REPLY_ERROR) {
        std::string err = sub->str ? sub->str : "subquery error";
        std::string qname = (i < qs.size()) ? std::string(qs[i].name) : "";
        freeReplyObject(exec_reply);
        throw gs::Error(ErrorCode::DB_QUERY, err, {{"query_name", qname}});
      }
    }
  }
  freeReplyObject(exec_reply);
}

std::vector<std::string> GraphClient::list_graphs() {
  std::vector<std::string> res;
  redisReply* reply = execute_command({"GRAPH.LIST"}, true, "GRAPH.LIST");
  if (reply->type == REDIS_REPLY_ARRAY) {
    for (size_t i = 0; i < reply->elements; ++i) {
      if (reply->element[i]->str) {
        res.emplace_back(reply->element[i]->str, reply->element[i]->len);
      }
    }
  }
  freeReplyObject(reply);
  return res;
}

void GraphClient::delete_graph(const std::string& graph) {
  try {
    redisReply* reply = execute_command({"GRAPH.DELETE", graph}, false, "GRAPH.DELETE");
    freeReplyObject(reply);
  } catch (const gs::Error& e) {
    // If graph does not exist or empty key, ignore
    if (contains_ignore_case(e.what(), "empty key") || contains_ignore_case(e.what(), "not exist")) {
      return;
    }
    throw;
  }
}

void GraphClient::constraint_unique(const std::string& graph, std::string_view label, std::string_view prop) {
  std::vector<std::string> argv = {
      "GRAPH.CONSTRAINT",
      "CREATE",
      graph,
      "UNIQUE",
      "NODE",
      std::string(label),
      "PROPERTIES",
      "1",
      std::string(prop)
  };

  ensure_connected();
  std::vector<const char*> argv_ptrs;
  std::vector<size_t> argv_lens;
  argv_ptrs.reserve(argv.size());
  argv_lens.reserve(argv.size());
  for (const auto& a : argv) {
    argv_ptrs.push_back(a.data());
    argv_lens.push_back(a.size());
  }

  void* r = redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv_ptrs.data(), argv_lens.data());
  if (!r) {
    std::string err = (ctx_ && ctx_->errstr[0] != '\0') ? ctx_->errstr : "null redis reply";
    disconnect();
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Redis constraint command failed: " + err);
  }

  redisReply* reply = static_cast<redisReply*>(r);
  if (reply->type == REDIS_REPLY_ERROR) {
    std::string err = reply->str ? reply->str : "";
    freeReplyObject(reply);
    // Ignore already exists
    if (contains_ignore_case(err, "already") || contains_ignore_case(err, "exists")) {
      return;
    }
    throw gs::Error(ErrorCode::DB_QUERY, "Failed to create constraint: " + err);
  }

  if (reply->type == REDIS_REPLY_STATUS && reply->str) {
    std::string_view status(reply->str, reply->len);
    if (status == "PENDING" || status == "OK" || contains_ignore_case(status, "already")) {
      freeReplyObject(reply);
      return;
    }
  }

  freeReplyObject(reply);
}

nlohmann::json GraphClient::raw(const std::vector<std::string>& argv) {
  if (argv.empty()) {
    return nullptr;
  }
  ensure_connected();

  std::vector<const char*> argv_ptrs;
  std::vector<size_t> argv_lens;
  argv_ptrs.reserve(argv.size());
  argv_lens.reserve(argv.size());
  for (const auto& a : argv) {
    argv_ptrs.push_back(a.data());
    argv_lens.push_back(a.size());
  }

  void* r = redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv_ptrs.data(), argv_lens.data());
  if (!r) {
    std::string err = (ctx_ && ctx_->errstr[0] != '\0') ? ctx_->errstr : "null redis reply";
    disconnect();
    throw gs::Error(ErrorCode::DB_UNAVAILABLE, "Redis raw command failed: " + err);
  }

  redisReply* reply = static_cast<redisReply*>(r);
  nlohmann::json j = reply_to_json(reply);
  freeReplyObject(reply);
  return j;
}

} // namespace gs::db
