#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "memory/briefing.hpp"
#include "memory/memory_store.hpp"
#include "memory/sessions.hpp"

using namespace gs;
using namespace gs::memory;

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
  } else {
    if (const char* h = std::getenv("GRAPHSTATE_FALKOR_HOST")) host = h;
    if (const char* p = std::getenv("GRAPHSTATE_FALKOR_PORT")) port = std::atoi(p);
  }
  return {host, port};
}

std::filesystem::path get_benchmark_project_path() {
#ifdef GS_TEST_FIXTURES_DIR
  auto p = std::filesystem::path(GS_TEST_FIXTURES_DIR) / "benchmark_project";
  if (std::filesystem::exists(p)) return p;
#endif
  std::vector<std::filesystem::path> candidates = {
      std::filesystem::current_path() / "tests" / "fixtures" / "benchmark_project",
      std::filesystem::current_path() / "fixtures" / "benchmark_project",
      std::filesystem::current_path().parent_path() / "tests" / "fixtures" / "benchmark_project",
      std::filesystem::current_path().parent_path().parent_path() / "tests" / "fixtures" / "benchmark_project"
  };
  for (const auto& c : candidates) {
    if (std::filesystem::exists(c)) return c;
  }
  return candidates[0];
}

} // namespace

TEST_CASE("integration: Memory Subgraph against FalkorDB") {
  auto [host, port] = get_falkor_endpoint();
  db::GraphClient client(host, port, "", 1000, 2000);

  if (!client.ping()) {
    MESSAGE("Skipping integration test: FalkorDB is not reachable at " << host << ":" << port);
    return;
  }

  core::Config config;
  config.root = get_benchmark_project_path();
  config.falkordb.host = host;
  config.falkordb.port = port;

  Engine engine(config);
  engine.initialize();

  // 1. Sessions & Intents
  auto& sessions = engine.sessions();
  const auto& ctx = sessions.ensure(nlohmann::json{{"name", "test-agent"}, {"version", "1.0"}});
  CHECK(!ctx.session_id.empty());
  CHECK(ctx.client == "test-agent");

  std::string i1 = sessions.upsert_intent("Add currency to totals");
  CHECK(!i1.empty());
  std::string i2 = sessions.upsert_intent("add currency to totals!");
  CHECK(i1 == i2); // Deduplication by normalized goal

  // 2. Remember: User Preference & Project Convention
  auto& mem = engine.memory_store();
  RememberRequest req1;
  req1.content = "Prefers type hints on all public functions.";
  req1.kind = "preference";
  auto res1 = mem.remember(req1, ctx);
  CHECK(!res1.id.empty());
  CHECK(res1.scope == "user");
  CHECK(!res1.deduplicated);

  // Deduplicate same preference
  auto res1_dup = mem.remember(req1, ctx);
  CHECK(res1_dup.id == res1.id);
  CHECK(res1_dup.deduplicated);

  // Project convention
  RememberRequest req2;
  req2.content = "Money is Decimal, never float.";
  req2.kind = "convention";
  req2.about = {"billing.py"};
  auto res2 = mem.remember(req2, ctx);
  CHECK(!res2.id.empty());
  CHECK(res2.scope == "project");
  CHECK(!res2.anchors.empty());

  // 3. Recall
  RecallRequest rreq;
  rreq.query = "Decimal money";
  rreq.about = "billing.py";
  rreq.limit = 5;
  auto rres = mem.recall(rreq, ctx);
  CHECK(!rres.results.empty());

  // 4. Briefing Generation
  auto briefing = generate_briefing("Test goal", ctx, engine);
  CHECK(!briefing.markdown.empty());
  CHECK(briefing.structured_content.contains("workspace"));
  CHECK(briefing.structured_content.contains("user_memories"));
  CHECK(briefing.structured_content.contains("project_memories"));

  // 5. End Session
  sessions.end("Completed testing", {i1}, {});
}
