#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "index/watcher.hpp"
#include "platform/platform.hpp"

using namespace gs;

namespace {

struct TempWorkspace {
  std::filesystem::path root;

  TempWorkspace() {
    root = std::filesystem::temp_directory_path() / ("gs_test_watcher_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

  void remove_file(const std::string& rel_path) {
    std::filesystem::path p = root / rel_path;
    std::error_code ec;
    std::filesystem::remove(p, ec);
  }
};

} // namespace

TEST_CASE("Engine: NoticeQueue unit tests") {
  TempWorkspace ws;
  core::Config config;
  config.root = ws.root;
  config.watch.enabled = false;
  Engine engine(std::move(config));

  CHECK(!engine.has_notices());
  CHECK(engine.drain_notices().empty());

  nlohmann::json notice1 = {{"kind", "test1"}, {"msg", "hello"}};
  nlohmann::json notice2 = {{"kind", "test2"}, {"msg", "world"}};

  engine.push_notice(notice1);
  CHECK(engine.has_notices());

  engine.push_notice(notice2);
  CHECK(engine.has_notices());

  auto drained = engine.drain_notices();
  CHECK(drained.size() == 2);
  CHECK(drained[0]["kind"] == "test1");
  CHECK(drained[1]["kind"] == "test2");

  CHECK(!engine.has_notices());
  CHECK(engine.drain_notices().empty());
}

TEST_CASE("Watcher: Detect file modifications and deletions") {
  TempWorkspace ws;
  ws.write_file("calc.py",
R"(def add(a: int, b: int) -> int:
    return a + b
)");

  ws.write_file("sub.py",
R"(def sub(a: int, b: int) -> int:
    return a - b
)");

  core::Config config;
  config.root = ws.root;
  config.watch.enabled = false; // Manual ticks
  config.watch.bulk_threshold = 10;
  Engine engine(std::move(config));

  engine.indexer().startup_reconcile();
  CHECK(engine.fact_store().contains("calc.py"));
  CHECK(engine.fact_store().contains("sub.py"));
  std::string old_hash = engine.fact_store().get("calc.py")->hash;

  SUBCASE("Detect content change") {
    // Wait briefly so mtime shifts if needed
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ws.write_file("calc.py",
R"(def add(a: int, b: int) -> int:
    """Updated docstring."""
    return a + b
)");

    engine.watcher().tick();

    const auto* facts = engine.fact_store().get("calc.py");
    REQUIRE(facts != nullptr);
    CHECK(facts->hash != old_hash);
  }

  SUBCASE("Detect file deletion") {
    ws.remove_file("sub.py");

    engine.watcher().tick();

    CHECK(!engine.fact_store().contains("sub.py"));
    CHECK(engine.fact_store().contains("calc.py"));
  }
}

TEST_CASE("Watcher: Ungated edit auditing triggers Notice") {
  TempWorkspace ws;
  ws.write_file("module_a.py",
R"(def compute(x: int) -> int:
    return x * 2
)");

  ws.write_file("module_b.py",
R"(from module_a import compute

def run():
    return compute(5)
)");

  core::Config config;
  config.root = ws.root;
  config.watch.enabled = false;
  Engine engine(std::move(config));

  engine.indexer().startup_reconcile();
  CHECK(engine.fact_store().contains("module_a.py"));
  CHECK(engine.fact_store().contains("module_b.py"));

  SUBCASE("Out-of-band edit introduces missing symbol violation") {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ws.write_file("module_b.py",
R"(from module_a import compute_missing

def run():
    return compute_missing(5)
)");

    engine.watcher().tick();

    // The audit should have detected MISSING_SYMBOL violation and pushed a notice
    CHECK(engine.has_notices());
    auto notices = engine.drain_notices();
    REQUIRE(!notices.empty());
    CHECK(notices[0]["kind"] == "ungated_edit");
    bool found_missing = false;
    for (const auto& code : notices[0]["violation_codes"]) {
      if (code == "MISSING_SYMBOL") found_missing = true;
    }
    CHECK(found_missing);
  }

  SUBCASE("Out-of-band edit introduces syntax error") {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ws.write_file("module_a.py",
R"(def compute(x: int -> int:
    return x * 2
)");

    engine.watcher().tick();

    CHECK(engine.has_notices());
    auto notices = engine.drain_notices();
    REQUIRE(!notices.empty());
    CHECK(notices[0]["kind"] == "ungated_edit");
    bool has_syntax = false;
    for (const auto& code : notices[0]["violation_codes"]) {
      if (code == "SYNTAX_ERROR") has_syntax = true;
    }
    CHECK(has_syntax);
  }
}

TEST_CASE("Watcher: Bulk change detection") {
  TempWorkspace ws;
  core::Config config;
  config.root = ws.root;
  config.watch.enabled = false;
  config.watch.bulk_threshold = 3; // Low threshold for testing

  ws.write_file("f1.py", "x = 1\n");
  ws.write_file("f2.py", "x = 2\n");
  ws.write_file("f3.py", "x = 3\n");
  ws.write_file("f4.py", "x = 4\n");
  ws.write_file("f5.py", "x = 5\n");

  Engine engine(std::move(config));
  engine.indexer().startup_reconcile();

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Modify 4 files (> bulk_threshold of 3)
  ws.write_file("f1.py", "x = 10\n");
  ws.write_file("f2.py", "x = 20\n");
  ws.write_file("f3.py", "x = 30\n");
  ws.write_file("f4.py", "x = 40\n");

  engine.watcher().tick();

  CHECK(engine.has_notices());
  auto notices = engine.drain_notices();
  REQUIRE(!notices.empty());
  CHECK(notices[0]["kind"] == "bulk_change");
  CHECK(notices[0]["likely"] == "vcs");
}

TEST_CASE("Watcher: Background thread start and stop") {
  TempWorkspace ws;
  ws.write_file("file.py", "val = 42\n");

  core::Config config;
  config.root = ws.root;
  config.watch.enabled = true;
  config.watch.interval_ms = 50; // fast tick for test
  Engine engine(std::move(config));

  engine.indexer().startup_reconcile();

  CHECK(!engine.watcher().is_running());
  engine.watcher().start();
  CHECK(engine.watcher().is_running());

  // Second start is idempotent
  engine.watcher().start();
  CHECK(engine.watcher().is_running());

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  ws.write_file("file.py", "val = 99\n");

  // Wait for background tick to pick it up
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  const auto* facts = engine.fact_store().get("file.py");
  REQUIRE(facts != nullptr);

  engine.watcher().stop();
  CHECK(!engine.watcher().is_running());

  // Second stop is idempotent
  engine.watcher().stop();
  CHECK(!engine.watcher().is_running());
}
