#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <cstdlib>
#include <filesystem>
#include <string>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "gate/gate.hpp"
#include "gate/patch.hpp"

using namespace gs;
using namespace gs::gate;

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

TEST_CASE("integration: Gate Verification against benchmark_project") {
  auto [host, port] = get_falkor_endpoint();
  db::GraphClient test_client(host, port, "", 3000, 5000);
  if (!test_client.ping()) {
    MESSAGE("Skipping integration test: FalkorDB is not reachable at " << host << ":" << port);
    return;
  }

  // Use temporary copy of benchmark_project so tests don't modify repository fixtures
  std::filesystem::path fixture_root = std::filesystem::current_path() / "tests" / "fixtures" / "benchmark_project";
  if (!std::filesystem::exists(fixture_root)) {
    fixture_root = std::filesystem::path("G:/GraphState/tests/fixtures/benchmark_project");
  }
  REQUIRE(std::filesystem::exists(fixture_root));

  std::filesystem::path temp_root = std::filesystem::temp_directory_path() / ("gs_test_gate_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::copy(fixture_root, temp_root, std::filesystem::copy_options::recursive);

  struct TempGuard {
    std::filesystem::path p;
    ~TempGuard() {
      std::error_code ec;
      std::filesystem::remove_all(p, ec);
    }
  } guard{temp_root};

  auto config = core::Config::load(temp_root);
  config.falkordb.host = host;
  config.falkordb.port = port;

  Engine engine(config);
  engine.initialize();
  engine.indexer().startup_reconcile();

  Gate gate(engine);

  SUBCASE("Single-file signature change is REJECTED with blast radius violations") {
    PatchRequest req;
    req.intent = "Add currency parameter to calculate_total";
    req.reasoning = "Totals must carry ISO currency";
    req.edits.push_back(Edit{
        .op = OpKind::ReplaceSymbol,
        .file = "billing.py",
        .symbol = "calculate_total",
        .code =
R"(def calculate_total(cart: Cart, currency: str, tax_rate: float = 0.1) -> Decimal:
    """Total including tax and currency."""
    subtotal = sum((i.price * i.qty for i in cart.items), Decimal("0"))
    return round_money(subtotal * (1 + Decimal(str(tax_rate))))
)"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Rejected);
    CHECK(res.failed_phase == "BLAST_RADIUS");
    CHECK(res.violations.size() >= 3);

    bool app_caller_found = false;
    for (const auto& v : res.violations) {
      if (v.code == "CALLER_INCOMPATIBLE") {
        if (v.location.has_value() && v.location->file == "app.py") {
          app_caller_found = true;
          CHECK(v.evidence.value("reason", "").find("missing required argument 'currency'") != std::string::npos);
        }
      }
    }
    CHECK(app_caller_found);
  }

  SUBCASE("Atomic multi-file update is APPLIED successfully") {
    PatchRequest req;
    req.intent = "Add default currency parameter and update callers";
    req.reasoning = "Add currency with default so it remains compatible";
    req.edits.push_back(Edit{
        .op = OpKind::ReplaceSymbol,
        .file = "billing.py",
        .symbol = "calculate_total",
        .code =
R"(def calculate_total(cart: Cart, tax_rate: float = 0.1, currency: str = "USD") -> Decimal:
    """Total including tax with currency."""
    subtotal = sum((i.price * i.qty for i in cart.items), Decimal("0"))
    return round_money(subtotal * (1 + Decimal(str(tax_rate))))
)"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Applied);
    CHECK(res.violations.empty());
    CHECK(res.changes.size() == 1);
    CHECK(res.changes[0].symbols_signature_changed.size() == 1);
  }
}
