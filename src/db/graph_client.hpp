#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include "core/error.hpp"
#include "db/result.hpp"

namespace gs::db {

struct Query {
  std::string_view name;
  std::string_view cypher;
  nlohmann::json params = nlohmann::json::object();
  bool read_only = false;
};

class GraphClient {
public:
  GraphClient(std::string host, int port, std::string password = "", int connect_timeout_ms = 5000, int query_timeout_ms = 30000);
  ~GraphClient();

  GraphClient(const GraphClient&) = delete;
  GraphClient& operator=(const GraphClient&) = delete;

  GraphClient(GraphClient&& other) noexcept;
  GraphClient& operator=(GraphClient&& other) noexcept;

  // PING -> PONG. Returns true if server responds, false on connection or protocol failure.
  // When force is false and the server recently failed, returns false immediately (circuit breaker).
  bool ping(bool force = false);

  // Clears circuit breaker state to allow immediate re-check.
  void reset_health();

  // Parses column 'j' of every row for read queries returning toJSON(...) AS j.
  std::vector<nlohmann::json> rows(const std::string& graph, const Query& q);

  // Returns the first parsed row, or json(nullptr) if no rows were returned.
  nlohmann::json one(const std::string& graph, const Query& q);

  // Executes a write query and returns execution statistics (QueryStats).
  QueryStats exec(const std::string& graph, const Query& q);

  // Pipelined queries: sends all, then reads all replies.
  // Returns per-query results in order. Throws on first error AFTER draining connection.
  std::vector<nlohmann::json> pipeline(const std::string& graph, const std::vector<Query>& qs);

  // MULTI ... EXEC: atomic batch of writes.
  void transaction(const std::string& graph, const std::vector<Query>& qs);

  // Lists all graph keys in FalkorDB (GRAPH.LIST).
  std::vector<std::string> list_graphs();

  // Deletes a graph key (GRAPH.DELETE). Idempotent (ignores non-existent graph errors).
  void delete_graph(const std::string& graph);

  // Creates a unique node constraint (GRAPH.CONSTRAINT CREATE).
  // Idempotent: ignores "already exists" errors.
  void constraint_unique(const std::string& graph, std::string_view label, std::string_view prop);

  // Debug command execution returning JSON (for cmd_query, cmd_doctor).
  nlohmann::json raw(const std::vector<std::string>& argv);

private:
  void connect();
  void disconnect();
  void ensure_connected();
  redisReply* execute_command(const std::vector<std::string>& argv, bool read_only, std::string_view query_name);

  std::string host_;
  int port_;
  std::string password_;
  int connect_timeout_ms_;
  int query_timeout_ms_;
  redisContext* ctx_ = nullptr;
  bool is_alive_cached_ = true;
  int64_t last_check_ms_ = 0;
  int64_t backoff_interval_ms_ = 2000;
};

} // namespace gs::db
