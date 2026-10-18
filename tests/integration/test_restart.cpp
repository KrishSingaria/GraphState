#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "core/clock.hpp"
#include "core/config.hpp"
#include "core/engine.hpp"
#include "db/queries.hpp"
#include "memory/sessions.hpp"
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
  } else {
    if (const char* h = std::getenv("GRAPHSTATE_FALKOR_HOST")) host = h;
    if (const char* p = std::getenv("GRAPHSTATE_FALKOR_PORT")) port = std::atoi(p);
  }
  return {host, port};
}

struct TempWorkspace {
  std::filesystem::path root;

  TempWorkspace() {
    root = std::filesystem::temp_directory_path() / ("gs_test_restart_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  void write_file(const std::string& rel_path, const std::string& content) {
    std::filesystem::path p = root / rel_path;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << content;
    out.flush();
  }

  bool file_exists(const std::string& rel_path) {
    std::error_code ec;
    return std::filesystem::exists(root / rel_path, ec);
  }
};

} // namespace

TEST_CASE("Restart: Offline edit reconcile on engine startup") {
  TempWorkspace ws;
  ws.write_file("service.py",
R"(def serve():
    return "v1"
)");

  auto [host, port] = get_falkor_endpoint();
  core::Config config;
  config.root = ws.root;
  config.falkordb.host = host;
  config.falkordb.port = port;
  config.watch.enabled = false;

  // Run initial session
  {
    Engine engine(config);
    engine.initialize();
    CHECK(engine.fact_store().contains("service.py"));
  }

  // Engine is now offline. Modify file on disk.
  ws.write_file("service.py",
R"(def serve():
    return "v2"
)");
  ws.write_file("new_module.py",
R"(def helper():
    return True
)");

  // Start a new engine instance
  {
    Engine engine(config);
    engine.initialize();

    CHECK(engine.fact_store().contains("service.py"));
    CHECK(engine.fact_store().contains("new_module.py"));

    const auto* facts = engine.fact_store().get("service.py");
    REQUIRE(facts != nullptr);
    bool found_serve = false;
    for (const auto& s : facts->symbols) {
      if (s.name == "serve") found_serve = true;
    }
    CHECK(found_serve);
  }
}

TEST_CASE("Restart: WAL rollback of interrupted transaction on startup") {
  TempWorkspace ws;
  auto [host, port] = get_falkor_endpoint();
  core::Config config;
  config.root = ws.root;
  config.falkordb.host = host;
  config.falkordb.port = port;
  config.watch.enabled = false;

  // Simulate an interrupted write: create a journal file in PREPARED state and an orphaned tmp file
  std::filesystem::path wal_dir = ws.root / ".graphstate" / "wal";
  std::filesystem::create_directories(wal_dir);

  std::string patch_id = "test_interrupted_patch";
  std::filesystem::path tmp_file = ws.root / "service.py.tmp";
  ws.write_file("service.py.tmp", "corrupted in-flight content");

  nlohmann::json journal = {
      {"patch_id", patch_id},
      {"state", "PREPARED"},
      {"files", {{
          {"path", "service.py"},
          {"tmp_path", "service.py.tmp"},
          {"op", "modified"}
      }}},
      {"created_dirs", nlohmann::json::array()}
  };

  std::filesystem::path journal_path = wal_dir / (patch_id + ".json");
  {
    std::ofstream out(journal_path);
    out << journal.dump(2);
  }

  CHECK(ws.file_exists("service.py.tmp"));

  // Start engine - initialize() must run WAL recovery and clean up tmp file
  {
    Engine engine(config);
    engine.initialize();

    CHECK(!ws.file_exists("service.py.tmp"));
  }
}

TEST_CASE("Restart: Mark abandoned session INTERRUPTED on startup") {
  auto [host, port] = get_falkor_endpoint();
  db::GraphClient client(host, port, "", 1000, 2000);
  if (!client.ping()) {
    MESSAGE("FalkorDB not running at " << host << ":" << port << " — skipping live DB session test");
    return;
  }

  TempWorkspace ws;
  core::Config config;
  config.root = ws.root;
  config.falkordb.host = host;
  config.falkordb.port = port;
  config.watch.enabled = false;

  std::string abandoned_session_id;

  // Session 1: Ensure session exists but exit without ending it
  {
    Engine engine(config);
    engine.initialize();

    const auto& sctx = engine.sessions().ensure({{"name", "test-agent"}});
    abandoned_session_id = sctx.session_id;
    REQUIRE(!abandoned_session_id.empty());
  }

  // Simulate an interrupted/crashed process (where clean shutdown didn't happen):
  // Reset status to ACTIVE and backdate last_seen_at past the 120s cutoff (§7.2)
  {
    int64_t old_ts = core::now_ms() - 150'000;
    client.exec(config.project_graph(), db::Query{
        .name = "simulate_crashed_session",
        .cypher = "MATCH (s:Session {id: $id}) SET s.status = 'ACTIVE', s.last_seen_at = $ts",
        .params = {{"id", abandoned_session_id}, {"ts", old_ts}},
        .read_only = false
    });
  }

  // Session 2: Start new engine instance, initialize() must mark previous session INTERRUPTED
  {
    Engine engine(config);
    engine.initialize();

    auto rows = engine.client().rows(engine.config().project_graph(), db::Query{
        .name = "get_session_status",
        .cypher = "MATCH (s:Session {id: $id}) RETURN toJSON({status: s.status}) AS j",
        .params = {{"id", abandoned_session_id}},
        .read_only = true
    });

    REQUIRE(!rows.empty());
    CHECK(rows[0]["status"] == "INTERRUPTED");

    engine.client().delete_graph(engine.config().project_graph());
  }
}
