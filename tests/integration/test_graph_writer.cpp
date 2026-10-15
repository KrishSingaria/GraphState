#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "db/graph_client.hpp"
#include "db/queries.hpp"
#include "platform/platform.hpp"

using namespace gs;

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

TEST_CASE("integration: GraphWriter and Indexer with benchmark_project") {
  auto [host, port] = get_falkor_endpoint();
  db::GraphClient test_client(host, port, "", 3000, 5000);
  REQUIRE(test_client.ping() == true);

  std::filesystem::path fixture_root = std::filesystem::current_path() / "tests" / "fixtures" / "benchmark_project";
  if (!std::filesystem::exists(fixture_root)) {
    fixture_root = std::filesystem::path("G:/GraphState/tests/fixtures/benchmark_project");
  }
  REQUIRE(std::filesystem::exists(fixture_root));

  auto config = core::Config::load(fixture_root);
  config.falkordb.host = host;
  config.falkordb.port = port;
  std::string test_graph = "gs:test_benchmark_" + config.workspace_key();
  test_client.delete_graph(test_graph);

  // Configure engine to use the isolated test graph
  Engine engine(std::move(config));
  engine.client().delete_graph(engine.config().project_graph());
  engine.initialize();

  // Full rebuild
  auto report = engine.indexer().full_rebuild();

  // 1. Assert exactly 8 files indexed (ignoring .graphstate/**)
  CHECK(report.scanned == 8);
  CHECK(report.parsed == 8);
  CHECK(report.changed == 8);
  CHECK(report.deleted == 0);
  CHECK(engine.fact_store().file_count() == 8);

  const auto& store = engine.fact_store();
  CHECK(store.contains("models.py"));
  CHECK(store.contains("utils.py"));
  CHECK(store.contains("billing.py"));
  CHECK(store.contains("checkout.py"));
  CHECK(store.contains("reports.py"));
  CHECK(store.contains("app.py"));
  CHECK(store.contains("web/api.ts"));
  CHECK(store.contains("web/cart.ts"));

  // 2. Query FalkorDB for callers of billing.py::calculate_total
  std::string project_graph = engine.config().project_graph();
  nlohmann::json callers_params = {
    {"ids", nlohmann::json::array({"billing.py::calculate_total"})},
    {"exclude_files", nlohmann::json::array()}
  };

  auto caller_rows = engine.client().rows(
      project_graph,
      db::Query{"callers_of_calc_total", db::Q_CALLERS_OF_IDS, callers_params, true});

  // Verify exactly 4 callers as required by §10.5
  REQUIRE(caller_rows.size() == 4);

  std::unordered_set<std::string> callers;
  for (const auto& r : caller_rows) {
    if (r.contains("caller")) {
      callers.insert(r["caller"].get<std::string>());
    }
  }

  CHECK(callers.contains("billing.py::Invoice.total"));
  CHECK(callers.contains("checkout.py::checkout"));
  CHECK(callers.contains("reports.py::daily_report"));
  CHECK(callers.contains("app.py::<module>"));

  // 3. Query FalkorDB for web/api.ts callers
  nlohmann::json ts_callers_params = {
    {"ids", nlohmann::json::array({"web/api.ts::fetchCart", "web/api.ts::totalLabel"})},
    {"exclude_files", nlohmann::json::array()}
  };
  auto ts_caller_rows = engine.client().rows(
      project_graph,
      db::Query{"ts_callers", db::Q_CALLERS_OF_IDS, ts_callers_params, true});

  std::unordered_set<std::string> ts_callers;
  for (const auto& r : ts_caller_rows) {
    if (r.contains("caller")) {
      ts_callers.insert(r["caller"].get<std::string>());
    }
  }
  CHECK(ts_callers.contains("web/cart.ts::CartView.render"));

  // 4. Verify node counts
  auto count_rows = engine.client().rows(
      project_graph,
      db::Query{"counts", db::Q_COUNTS, nlohmann::json::object(), true});

  std::unordered_map<std::string, int64_t> node_counts;
  for (const auto& r : count_rows) {
    if (r.contains("label") && r.contains("n")) {
      node_counts[r["label"].get<std::string>()] = r["n"].get<int64_t>();
    }
  }

  CHECK(node_counts["File"] == 8);
  CHECK(node_counts["Function"] >= 10);
  CHECK(node_counts["Class"] >= 3);

  // 5. Test incremental reconcile (no changes -> 0 changed, 0 deleted)
  auto reconcile_report = engine.indexer().startup_reconcile();
  CHECK(reconcile_report.scanned == 8);
  CHECK(reconcile_report.changed == 0);
  CHECK(reconcile_report.deleted == 0);

  // 6. Test freshness check
  CHECK(engine.indexer().ensure_fresh("billing.py") == false);

  // Clean up
  engine.client().delete_graph(project_graph);
}
