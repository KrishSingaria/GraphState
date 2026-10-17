#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "core/clock.hpp"
#include "core/config.hpp"
#include "core/engine.hpp"
#include "gate/changesets.hpp"
#include "gate/gate.hpp"
#include "gate/patch.hpp"
#include "mcp/tool_registry.hpp"

using namespace gs;
using namespace gs::gate;

namespace {

struct TempWorkspace {
  std::filesystem::path root;

  TempWorkspace() {
    root = std::filesystem::temp_directory_path() / ("gs_test_cs_" + std::to_string(core::now_ms()) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
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
  }

  std::string read_file(const std::string& rel_path) {
    std::filesystem::path p = root / rel_path;
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

  bool file_exists(const std::string& rel_path) {
    std::error_code ec;
    return std::filesystem::exists(root / rel_path, ec);
  }
};

} // namespace

TEST_CASE("Changeset: Lifecycle, incremental building, obligations, and discharge") {
  TempWorkspace ws;
  ws.write_file("main.py",
R"(def run():
    print("hello")
)");

  core::Config cfg = core::Config::load(ws.root);
  Engine engine(cfg);
  engine.indexer().startup_reconcile();

  memory::SessionCtx session{"s_test_cs_01", "", "test"};

  // 1. Begin changeset
  auto cs_init = engine.changesets().begin("Build new order feature", session);
  CHECK(cs_init.id.starts_with("c_"));
  CHECK(cs_init.status == "OPEN");
  CHECK(cs_init.intent == "Build new order feature");
  CHECK(cs_init.obligations.empty());

  // Cannot begin second changeset while one is OPEN
  CHECK_THROWS_AS(engine.changesets().begin("Another", session), mcp::ToolError);

  // 2. Propose patch that imports uncreated module
  PatchRequest req1;
  req1.intent = "Add routes";
  req1.reasoning = "Create routes calling services";
  Edit e1;
  e1.op = OpKind::CreateFile;
  e1.file = "routes.py";
  e1.content =
R"(from services import create_order

def handle_request():
    return create_order()
)";
  req1.edits.push_back(std::move(e1));

  PatchResult res1 = engine.gate().propose(req1, session);
  CHECK(res1.status == PatchStatus::Applied);
  REQUIRE(res1.changeset.has_value());
  CHECK((*res1.changeset)["open_obligations"] == 1);
  CHECK((*res1.changeset)["obligations"][0]["code"] == "MISSING_MODULE");

  // 3. Attempting to finalize with open obligations keeps it OPEN
  auto cs_fin1 = engine.changesets().finalize(session, false);
  CHECK(cs_fin1.status == "OPEN");
  CHECK(cs_fin1.obligations.size() == 1);

  // 4. Propose patch creating the missing module
  PatchRequest req2;
  req2.intent = "Add services";
  req2.reasoning = "Implement create_order";
  Edit e2;
  e2.op = OpKind::CreateFile;
  e2.file = "services.py";
  e2.content =
R"(def create_order() -> int:
    return 100
)";
  req2.edits.push_back(std::move(e2));

  PatchResult res2 = engine.gate().propose(req2, session);
  CHECK(res2.status == PatchStatus::Applied);
  REQUIRE(res2.changeset.has_value());
  CHECK((*res2.changeset)["open_obligations"] == 0);
  CHECK((*res2.changeset)["discharged"].size() == 1);

  // 5. Finalize now cleanly closes the changeset
  auto cs_fin2 = engine.changesets().finalize(session, false);
  CHECK(cs_fin2.status == "CLOSED");
  CHECK(engine.changesets().current() == std::nullopt);
}

TEST_CASE("Changeset: Abandon rollbacks and conflict detection") {
  TempWorkspace ws;
  ws.write_file("config.py", "VERSION = 1\n");

  core::Config cfg = core::Config::load(ws.root);
  Engine engine(cfg);
  engine.indexer().startup_reconcile();

  memory::SessionCtx session{"s_test_cs_02", "", "test"};

  // 1. Begin changeset
  engine.changesets().begin("Experimental feature", session);

  // 2. Modify config.py and create extra.py
  PatchRequest req;
  req.intent = "Update version and add extra";
  req.reasoning = "Testing rollback";

  Edit e1;
  e1.op = OpKind::ReplaceText;
  e1.file = "config.py";
  e1.old_text = "VERSION = 1";
  e1.new_text = "VERSION = 2";
  req.edits.push_back(std::move(e1));

  Edit e2;
  e2.op = OpKind::CreateFile;
  e2.file = "extra.py";
  e2.content = "DATA = 42\n";
  req.edits.push_back(std::move(e2));

  PatchResult res = engine.gate().propose(req, session);
  CHECK(res.status == PatchStatus::Applied);
  CHECK(ws.read_file("config.py") == "VERSION = 2\n");
  CHECK(ws.file_exists("extra.py"));

  // 3. Conflict detection: modify config.py out of band
  ws.write_file("config.py", "VERSION = 999\n");

  auto cs_ab1 = engine.changesets().abandon(session, false);
  CHECK(!cs_ab1.conflicts.empty());
  CHECK(cs_ab1.conflicts[0] == "config.py");
  // Still open because of conflict
  CHECK(engine.changesets().current().has_value());

  // 4. Force abandon: should revert config.py and remove extra.py
  auto cs_ab2 = engine.changesets().abandon(session, true);
  CHECK(cs_ab2.status == "ABANDONED");
  CHECK(engine.changesets().current() == std::nullopt);

  CHECK(ws.read_file("config.py") == "VERSION = 1\n");
  CHECK_FALSE(ws.file_exists("extra.py"));
}
