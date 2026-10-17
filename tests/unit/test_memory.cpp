#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include "core/clock.hpp"
#include "core/config.hpp"
#include "core/engine.hpp"
#include "mcp/tool_registry.hpp"
#include "memory/memory_store.hpp"
#include "memory/sessions.hpp"

using namespace gs;
using namespace gs::memory;

namespace {

struct TempWorkspace {
  std::filesystem::path root;

  TempWorkspace() {
    auto tmp = std::filesystem::temp_directory_path() / ("gs_test_mem_" + std::to_string(core::now_ms()));
    std::filesystem::create_directories(tmp);
    root = tmp;
  }

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

} // namespace

TEST_SUITE("memory::unit") {

TEST_CASE("Secret Guard detects credentials and passes harmless content") {
  // Positive matches (must detect)
  CHECK(MemoryStore::contains_secret("Deploy with AKIAIOSFODNN7EXAMPLE key"));
  CHECK(MemoryStore::contains_secret("-----BEGIN RSA PRIVATE KEY-----\nMIIEowIBAAKCAQEA...\n-----END RSA PRIVATE KEY-----"));
  CHECK(MemoryStore::contains_secret("My token is ghp_1234567890abcdefghijklmnopqrstuvwxyz"));
  CHECK(MemoryStore::contains_secret("sk-abcdefghijklmnopqrstuvwxyz1234567890"));
  CHECK(MemoryStore::contains_secret("Slack token xoxb-1234567890-123456789012-abcdefghijklmnopqrstuvwx"));
  CHECK(MemoryStore::contains_secret("password = SuperSecretP@ssw0rd123"));
  CHECK(MemoryStore::contains_secret("secret: \"TopSecretTokenValue99\""));

  // Negative matches (must NOT detect)
  CHECK_FALSE(MemoryStore::contains_secret("Money is Decimal, never float."));
  CHECK_FALSE(MemoryStore::contains_secret("token_count = len(tokens)"));
  CHECK_FALSE(MemoryStore::contains_secret("function authenticateUser(username, passwordHash) {}"));
  CHECK_FALSE(MemoryStore::contains_secret("Prefer pytest over unittest."));
}

TEST_CASE("Intent goal normalization") {
  CHECK(Sessions::normalize_goal("  Build Order API!  ") == "build order api");
  CHECK(Sessions::normalize_goal("Refactor   Billing  Logic...") == "refactor billing logic");
  CHECK(Sessions::normalize_goal("Add currency to totals?;;") == "add currency to totals");
  CHECK(Sessions::normalize_goal("Fix bug in checkout:\n\t") == "fix bug in checkout");
  CHECK(Sessions::normalize_goal("") == "");
}

TEST_CASE("Memory content normalization") {
  CHECK(MemoryStore::normalize_content("  Money is   Decimal, never float.  ") == "money is decimal, never float.");
  CHECK(MemoryStore::normalize_content("Prefer pytest\n\tover unittest.") == "prefer pytest over unittest.");
}

TEST_CASE("Fulltext query sanitization") {
  CHECK(MemoryStore::sanitize_fulltext_query("billing - calculate_total()") == "billing calculate_total");
  CHECK(MemoryStore::sanitize_fulltext_query("foo:bar [baz]") == "foo bar baz");
  CHECK(MemoryStore::sanitize_fulltext_query("hello_world 123") == "hello_world 123");
}

TEST_CASE("MemoryStore secret guard throws ToolError") {
  TempWorkspace ws;
  core::Config cfg;
  cfg.root = ws.root;
  cfg.falkordb.port = 6399; // intentionally not running for offline test

  Engine engine(cfg);
  MemoryStore store(engine);
  SessionCtx session{"s_test", "i_test", "test-agent"};

  RememberRequest req;
  req.content = "AWS key is AKIAIOSFODNN7EXAMPLE";
  req.kind = "fact";

  CHECK_THROWS_AS(store.remember(req, session), mcp::ToolError);

  try {
    store.remember(req, session);
  } catch (const mcp::ToolError& e) {
    CHECK(e.code == "SECRET_DETECTED");
  }
}

TEST_CASE("MemoryStore invalid anchor throws INVALID_ANCHOR") {
  TempWorkspace ws;
  core::Config cfg;
  cfg.root = ws.root;
  cfg.falkordb.port = 6399;

  Engine engine(cfg);
  MemoryStore store(engine);
  SessionCtx session{"s_test", "i_test", "test-agent"};

  RememberRequest req;
  req.content = "Valid convention content";
  req.kind = "convention";
  req.about = {"nonexistent_function_12345"};

  CHECK_THROWS_AS(store.remember(req, session), mcp::ToolError);

  try {
    store.remember(req, session);
  } catch (const mcp::ToolError& e) {
    CHECK(e.code == "INVALID_ANCHOR");
  }
}

TEST_CASE("Recall without query or about throws EMPTY_QUERY") {
  TempWorkspace ws;
  core::Config cfg;
  cfg.root = ws.root;
  cfg.falkordb.port = 6399;

  Engine engine(cfg);
  MemoryStore store(engine);
  SessionCtx session{"s_test", "i_test", "test-agent"};

  RecallRequest req;
  req.query = "";
  req.about = "";

  CHECK_THROWS_AS(store.recall(req, session), mcp::ToolError);

  try {
    store.recall(req, session);
  } catch (const mcp::ToolError& e) {
    CHECK(e.code == "EMPTY_QUERY");
  }
}

} // TEST_SUITE
