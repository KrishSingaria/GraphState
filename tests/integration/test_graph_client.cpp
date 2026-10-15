#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/error.hpp"
#include "db/graph_client.hpp"
#include "db/queries.hpp"
#include "db/schema.hpp"

using namespace gs;
using namespace gs::db;

namespace {

std::pair<std::string, int> get_falkor_endpoint() {
  std::string host = "127.0.0.1";
  int port = 6390;
  if (const char* env = std::getenv("GS_FALKOR")) {
    std::string s(env);
    size_t col = s.find(':');
    if (col != std::string::npos) {
      host = s.substr(0, col);
      port = std::atoi(s.substr(col + 1).c_str());
    } else {
      host = s;
    }
  }
  return {host, port};
}

} // namespace

TEST_CASE("integration: GraphClient ping and basic CRUD") {
  auto [host, port] = get_falkor_endpoint();
  GraphClient client(host, port, "", 3000, 5000);

  REQUIRE(client.ping() == true);

  const std::string test_graph = "gs:test_integration";
  client.delete_graph(test_graph);

  // 1. Write query via exec()
  QueryStats qs = client.exec(test_graph, Query{
      "create_file",
      "CREATE (f:File {id: 'src/main.py', size: 1024})",
      nlohmann::json::object(),
      false
  });
  CHECK(qs.nodes_created == 1);
  CHECK(qs.props_set == 2);

  // 2. Read query via rows()
  auto rows = client.rows(test_graph, Query{
      "read_file",
      "MATCH (f:File {id: 'src/main.py'}) RETURN toJSON({id: f.id, size: f.size}) AS j",
      nlohmann::json::object(),
      true
  });
  REQUIRE(rows.size() == 1);
  CHECK(rows[0]["id"] == "src/main.py");
  CHECK(rows[0]["size"] == 1024);

  // 3. Read query via one()
  auto one_row = client.one(test_graph, Query{
      "read_one",
      "MATCH (f:File {id: 'src/main.py'}) RETURN toJSON({id: f.id}) AS j",
      nlohmann::json::object(),
      true
  });
  CHECK(one_row.is_object());
  CHECK(one_row["id"] == "src/main.py");

  auto not_found = client.one(test_graph, Query{
      "read_not_found",
      "MATCH (f:File {id: 'does_not_exist'}) RETURN toJSON({id: f.id}) AS j",
      nlohmann::json::object(),
      true
  });
  CHECK(not_found.is_null());

  // 4. Parameterized query
  auto param_rows = client.rows(test_graph, Query{
      "read_param",
      "MATCH (f:File {id: $fid}) RETURN toJSON({id: f.id, size: f.size}) AS j",
      nlohmann::json{{"fid", "src/main.py"}},
      true
  });
  REQUIRE(param_rows.size() == 1);
  CHECK(param_rows[0]["id"] == "src/main.py");

  // Clean up
  client.delete_graph(test_graph);
}

TEST_CASE("integration: GraphClient pipeline and transaction") {
  auto [host, port] = get_falkor_endpoint();
  GraphClient client(host, port, "", 3000, 5000);
  REQUIRE(client.ping() == true);

  const std::string test_graph = "gs:test_pipeline";
  client.delete_graph(test_graph);

  // Pipeline execution
  std::vector<Query> pipe_queries = {
      Query{"p1", "CREATE (n:Item {id: 1})", nlohmann::json::object(), false},
      Query{"p2", "CREATE (n:Item {id: 2})", nlohmann::json::object(), false},
      Query{"p3", "MATCH (n:Item) RETURN toJSON({id: n.id}) AS j", nlohmann::json::object(), true}
  };

  auto pipe_results = client.pipeline(test_graph, pipe_queries);
  REQUIRE(pipe_results.size() == 3);
  CHECK(pipe_results[0]["nodes_created"] == 1);
  CHECK(pipe_results[1]["nodes_created"] == 1);
  CHECK(pipe_results[2].is_array());
  CHECK(pipe_results[2].size() == 2);

  // Transaction (MULTI/EXEC) execution
  std::vector<Query> tx_queries = {
      Query{"tx1", "CREATE (n:TxItem {id: 'a'})", nlohmann::json::object(), false},
      Query{"tx2", "CREATE (n:TxItem {id: 'b'})", nlohmann::json::object(), false}
  };
  CHECK_NOTHROW(client.transaction(test_graph, tx_queries));

  auto tx_rows = client.rows(test_graph, Query{
      "verify_tx",
      "MATCH (n:TxItem) RETURN toJSON({id: n.id}) AS j",
      nlohmann::json::object(),
      true
  });
  CHECK(tx_rows.size() == 2);

  // Clean up
  client.delete_graph(test_graph);
}

TEST_CASE("integration: Schema bootstrap and idempotency") {
  auto [host, port] = get_falkor_endpoint();
  GraphClient client(host, port, "", 3000, 5000);
  REQUIRE(client.ping() == true);

  const std::string test_graph = "gs:test_schema";
  const std::string user_graph = "gs:test_user_schema";
  client.delete_graph(test_graph);
  client.delete_graph(user_graph);

  // First run: bootstrap should succeed cleanly
  CHECK_NOTHROW(bootstrap_project_schema(client, test_graph));
  CHECK_NOTHROW(bootstrap_user_schema(client, user_graph));

  // Second run: bootstrap must be idempotent (no throw on existing indexes/constraints)
  CHECK_NOTHROW(bootstrap_project_schema(client, test_graph));
  CHECK_NOTHROW(bootstrap_user_schema(client, user_graph));

  // Meta upsert and get
  CHECK_NOTHROW(ensure_meta(client, test_graph, "/workspace/root", "test_workspace"));

  auto meta = client.one(test_graph, Query{"get_meta", Q_META_GET, nlohmann::json::object(), true});
  REQUIRE(meta.is_object());
  CHECK(meta["schema_version"] == 1);

  // Clean up
  client.delete_graph(test_graph);
  client.delete_graph(user_graph);
}
