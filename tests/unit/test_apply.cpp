#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "gate/gate.hpp"
#include "gate/patch.hpp"

using namespace gs;
using namespace gs::gate;

namespace {

struct TempWorkspace {
  std::filesystem::path root;

  TempWorkspace() {
    root = std::filesystem::temp_directory_path() / ("gs_test_apply_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
};

} // namespace

TEST_CASE("Gate: P1 buffer edit operations") {
  TempWorkspace ws;
  ws.write_file("calc.py",
R"(def add(a: int, b: int) -> int:
    """Add two numbers."""
    return a + b


class Math:
    def square(self, x: int) -> int:
        return x * x
)");

  core::Config cfg = core::Config::load(ws.root);
  Engine engine(cfg);
  engine.indexer().startup_reconcile();
  Gate gate(engine);

  SUBCASE("replace_symbol with dedent and reindent") {
    PatchRequest req;
    req.intent = "Optimize add function";
    req.reasoning = "Add docstring and inline type assertion";
    req.dry_run = true;
    req.edits.push_back(Edit{
        .op = OpKind::ReplaceSymbol,
        .file = "calc.py",
        .symbol = "add",
        .code =
R"(def add(a: int, b: int) -> int:
    """Optimized addition."""
    return a + b)"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Validated);
    CHECK(res.violations.empty());
    CHECK(res.changes.size() == 1);
    CHECK(res.changes[0].symbols_body_changed.size() == 1);
    CHECK(res.changes[0].symbols_body_changed[0] == "calc.py::add");
  }

  SUBCASE("replace_symbol symbol not found gives suggestion") {
    PatchRequest req;
    req.intent = "Replace non-existent symbol";
    req.reasoning = "Test error handling";
    req.dry_run = true;
    req.edits.push_back(Edit{
        .op = OpKind::ReplaceSymbol,
        .file = "calc.py",
        .symbol = "ad", // typo for add
        .code = "def ad(): pass"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Rejected);
    CHECK(res.failed_phase == "EDIT");
    REQUIRE(!res.violations.empty());
    CHECK(res.violations[0].code == "SYMBOL_NOT_FOUND");
    CHECK(res.violations[0].evidence.contains("candidates"));
  }

  SUBCASE("replace_text exact replacement") {
    PatchRequest req;
    req.intent = "Change return expression in Math.square";
    req.reasoning = "Use power operator";
    req.dry_run = true;
    req.edits.push_back(Edit{
        .op = OpKind::ReplaceText,
        .file = "calc.py",
        .old_text = "return x * x",
        .new_text = "return x ** 2"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Validated);
    CHECK(res.violations.empty());
  }

  SUBCASE("replace_text not found reports closest line") {
    PatchRequest req;
    req.intent = "Replace typo text";
    req.reasoning = "Test error";
    req.dry_run = true;
    req.edits.push_back(Edit{
        .op = OpKind::ReplaceText,
        .file = "calc.py",
        .old_text = "return x * y",
        .new_text = "return 0"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Rejected);
    CHECK(res.failed_phase == "EDIT");
    REQUIRE(!res.violations.empty());
    CHECK(res.violations[0].code == "TEXT_NOT_FOUND");
    CHECK(res.violations[0].evidence.contains("closest_line"));
  }

  SUBCASE("insert_symbol into class") {
    PatchRequest req;
    req.intent = "Add cube method to Math";
    req.reasoning = "Expand Math class";
    req.dry_run = true;
    InsertAnchor anchor;
    anchor.into_class = "Math";
    req.edits.push_back(Edit{
        .op = OpKind::InsertSymbol,
        .file = "calc.py",
        .code =
R"(def cube(self, x: int) -> int:
    return x * x * x)",
        .anchor = anchor
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Validated);
    CHECK(res.violations.empty());
    CHECK(res.changes.size() == 1);
    CHECK(res.changes[0].symbols_added.size() == 1);
    CHECK(res.changes[0].symbols_added[0] == "calc.py::Math.cube");
  }

  SUBCASE("create_file and delete_file") {
    PatchRequest req;
    req.intent = "Create new module";
    req.reasoning = "Add helper functions";
    req.dry_run = true;
    req.edits.push_back(Edit{
        .op = OpKind::CreateFile,
        .file = "helpers.py",
        .content = "def identity(x):\n    return x\n"
    });

    auto res = gate.propose(req);
    CHECK(res.status == PatchStatus::Validated);
    CHECK(res.violations.empty());
    CHECK(res.changes.size() == 1);
    CHECK(res.changes[0].change == "created");
  }
}
