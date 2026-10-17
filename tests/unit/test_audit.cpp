#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "core/config.hpp"
#include "gate/audit.hpp"
#include "platform/platform.hpp"

using namespace gs;
using namespace gs::gate;

namespace {

struct TempDir {
  std::filesystem::path path;

  TempDir(std::string_view prefix) {
    path = std::filesystem::temp_directory_path() / ("gs_test_audit_" + std::string(prefix) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  void write_file(const std::string& rel, const std::string& content) {
    auto full = path / platform::from_utf8(rel);
    std::filesystem::create_directories(full.parent_path());
    std::ofstream ofs(full, std::ios::binary);
    ofs << content;
  }
};

} // namespace

TEST_CASE("audit_workspace on clean workspace returns zero findings") {
  TempDir dir("clean");
  dir.write_file("shop/billing.py",
      "def calculate_total(amount: int, currency: str = 'USD') -> int:\n"
      "    return amount\n");
  dir.write_file("shop/checkout.py",
      "from shop.billing import calculate_total\n"
      "def checkout() -> int:\n"
      "    return calculate_total(100, 'EUR')\n");

  core::Config config;
  config.root = dir.path;

  auto report = audit_workspace(config, dir.path);
  CHECK(report.files == 2);
  CHECK(report.findings.empty());
  CHECK(report.counts.empty());
}

TEST_CASE("audit_workspace detects syntax errors") {
  TempDir dir("syntax");
  dir.write_file("app.py",
      "def valid():\n"
      "    return 1\n");
  dir.write_file("broken.py",
      "def bad(:\n"
      "    pass\n");

  core::Config config;
  config.root = dir.path;

  auto report = audit_workspace(config, dir.path);
  CHECK(report.files == 2);
  CHECK(report.findings.size() >= 1);
  CHECK(report.counts["SYNTAX_ERROR"] >= 1);

  bool found_broken = false;
  for (const auto& f : report.findings) {
    if (f.code == "SYNTAX_ERROR" && f.file == "broken.py") {
      found_broken = true;
      break;
    }
  }
  CHECK(found_broken);
}

TEST_CASE("audit_workspace detects call incompatibility") {
  TempDir dir("binding");
  dir.write_file("billing.py",
      "def calculate_total(cart: dict, currency: str) -> int:\n"
      "    return 100\n");
  dir.write_file("orders.py",
      "from billing import calculate_total\n"
      "def place_order():\n"
      "    return calculate_total({})\n"); // missing currency

  core::Config config;
  config.root = dir.path;

  auto report = audit_workspace(config, dir.path);
  CHECK(report.counts["CALL_INCOMPATIBLE"] >= 1);

  bool found = false;
  for (const auto& f : report.findings) {
    if (f.code == "CALL_INCOMPATIBLE" && f.file == "orders.py") {
      found = true;
      CHECK(f.callee.find("calculate_total") != std::string::npos);
      break;
    }
  }
  CHECK(found);
}

TEST_CASE("audit_workspace detects missing symbols and broken imports") {
  TempDir dir("imports");
  dir.write_file("utils/money.py",
      "def format_currency(val: float) -> str:\n"
      "    return f'${val}'\n");
  dir.write_file("reports.py",
      "from utils.money import format_money\n" // Broken import: format_money does not exist
      "import utils.money as money\n"
      "def generate():\n"
      "    money.non_existent_func()\n"); // Missing symbol call

  core::Config config;
  config.root = dir.path;

  auto report = audit_workspace(config, dir.path);
  CHECK(report.counts["BROKEN_IMPORT"] >= 1);
  CHECK(report.counts["MISSING_SYMBOL"] >= 1);

  bool found_import = false;
  bool found_symbol = false;
  for (const auto& f : report.findings) {
    if (f.code == "BROKEN_IMPORT" && f.callee.find("format_money") != std::string::npos) {
      found_import = true;
    }
    if (f.code == "MISSING_SYMBOL" && f.callee.find("non_existent_func") != std::string::npos) {
      found_symbol = true;
    }
  }
  CHECK(found_import);
  CHECK(found_symbol);
}

TEST_CASE("audit_workspace detects missing relative modules") {
  TempDir dir("modules");
  dir.write_file("shop/orders.py",
      "from .non_existent_module import something\n"
      "def run():\n"
      "    pass\n");

  core::Config config;
  config.root = dir.path;

  auto report = audit_workspace(config, dir.path);
  CHECK(report.counts["MISSING_MODULE"] >= 1);

  bool found = false;
  for (const auto& f : report.findings) {
    if (f.code == "MISSING_MODULE" && f.file == "shop/orders.py") {
      found = true;
      break;
    }
  }
  CHECK(found);
}

TEST_CASE("audit_workspace diffs against base and reports only introduced findings") {
  TempDir base("diff_base");
  base.write_file("pre_existing_bad.py",
      "def bad(:\n"
      "    pass\n");
  base.write_file("calc.py",
      "def add(a: int, b: int) -> int:\n"
      "    return a + b\n");

  TempDir current("diff_current");
  // Keep same pre-existing error
  current.write_file("pre_existing_bad.py",
      "def bad(:\n"
      "    pass\n");
  current.write_file("calc.py",
      "def add(a: int, b: int) -> int:\n"
      "    return a + b\n");
  // Introduce a new call incompatibility
  current.write_file("use.py",
      "from calc import add\n"
      "def run():\n"
      "    return add(1)\n"); // missing 'b'

  core::Config config;

  auto report = audit_workspace(config, current.path, base.path);

  // Pre-existing SYNTAX_ERROR in pre_existing_bad.py must NOT be reported as introduced
  CHECK(report.counts.find("SYNTAX_ERROR") == report.counts.end());
  CHECK(report.counts["CALL_INCOMPATIBLE"] == 1);
  CHECK(report.findings.size() == 1);
  CHECK(report.findings[0].file == "use.py");

  // Verify JSON output serialization
  auto j = report.to_json();
  CHECK(j.contains("findings"));
  CHECK(j.contains("counts"));
  CHECK(j.contains("files"));
  CHECK(j["findings"].size() == 1);
  CHECK(j["findings"][0]["code"] == "CALL_INCOMPATIBLE");
}
