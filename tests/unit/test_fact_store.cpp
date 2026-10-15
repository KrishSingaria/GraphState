#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include "index/fact_store.hpp"
#include "platform/platform.hpp"

using namespace gs;
using namespace gs::index;
using namespace gs::lang;

namespace {

SymbolFact make_sym(std::string id, std::string name, std::string qualname, SymbolKind kind = SymbolKind::Function) {
  SymbolFact s;
  s.id = std::move(id);
  s.name = std::move(name);
  s.qualname = std::move(qualname);
  s.kind = kind;
  return s;
}

FileFacts make_facts(std::string file, uint64_t size = 100, int64_t mtime = 1000) {
  FileFacts f;
  f.file = std::move(file);
  f.size = size;
  f.mtime_ns = mtime;
  f.hash = "deadbeef";
  return f;
}

} // namespace

TEST_CASE("FactStore basic put, get, update, and erase") {
  FactStore store;

  auto f1 = make_facts("src/calc.py", 200, 5000);
  f1.symbols.push_back(make_sym("src/calc.py::add", "add", "add"));
  f1.symbols.push_back(make_sym("src/calc.py::sub", "sub", "sub"));

  store.put(std::move(f1));

  CHECK(store.file_count() == 1);
  CHECK(store.total_symbols() == 2);
  CHECK(store.contains("src/calc.py"));

  const auto* facts = store.get("src/calc.py");
  REQUIRE(facts != nullptr);
  CHECK(facts->size == 200);

  // Update file: replace with 1 symbol
  auto f1_up = make_facts("src/calc.py", 250, 6000);
  f1_up.symbols.push_back(make_sym("src/calc.py::add", "add", "add"));
  store.put(std::move(f1_up));

  CHECK(store.file_count() == 1);
  CHECK(store.total_symbols() == 1);
  CHECK(store.find_symbol_by_id("src/calc.py::sub") == nullptr);
  CHECK(store.find_symbol_by_id("src/calc.py::add") != nullptr);

  // Erase
  store.erase("src/calc.py");
  CHECK(store.file_count() == 0);
  CHECK(store.total_symbols() == 0);
  CHECK(store.get("src/calc.py") == nullptr);
}

TEST_CASE("FactStore O(1) symbol lookup by ID") {
  FactStore store;

  auto f = make_facts("models.py");
  f.symbols.push_back(make_sym("models.py::Cart", "Cart", "Cart", SymbolKind::Class));
  f.symbols.push_back(make_sym("models.py::Cart.__init__", "__init__", "Cart.__init__", SymbolKind::Constructor));
  f.symbols.push_back(make_sym("models.py::Cart.total", "total", "Cart.total", SymbolKind::Method));

  store.put(std::move(f));

  const auto* sym = store.find_symbol_by_id("models.py::Cart.total");
  REQUIRE(sym != nullptr);
  CHECK(sym->name == "total");
  CHECK(sym->qualname == "Cart.total");
  CHECK(sym->kind == SymbolKind::Method);

  CHECK(store.find_symbol_by_id("models.py::NonExistent") == nullptr);
}

TEST_CASE("FactStore multimap lookup by name and qualname") {
  FactStore store;

  auto f1 = make_facts("billing.py");
  f1.symbols.push_back(make_sym("billing.py::total", "total", "total"));
  f1.symbols.push_back(make_sym("billing.py::Invoice.total", "total", "Invoice.total", SymbolKind::Method));

  auto f2 = make_facts("cart.py");
  f2.symbols.push_back(make_sym("cart.py::Cart.total", "total", "Cart.total", SymbolKind::Method));
  f2.symbols.push_back(make_sym("cart.py::checkout", "checkout", "checkout"));

  store.put(std::move(f1));
  store.put(std::move(f2));

  // Find by name "total" matches all 3
  auto by_name = store.find_symbols_by_name("total");
  CHECK(by_name.size() == 3);

  // Find by qualname matches exactly 1
  auto by_qual = store.find_symbols_by_qualname("Invoice.total");
  REQUIRE(by_qual.size() == 1);
  CHECK(by_qual[0]->id == "billing.py::Invoice.total");

  // Erasing billing.py cleans up both symbols from multimaps
  store.erase("billing.py");
  CHECK(store.find_symbols_by_name("total").size() == 1);
  CHECK(store.find_symbols_by_qualname("Invoice.total").empty());
}

TEST_CASE("FactStore prioritized symbol search: exact ID -> qualname -> name") {
  FactStore store;

  // sym1: ID exact
  auto f1 = make_facts("service.py");
  f1.symbols.push_back(make_sym("service.py::handler", "handler", "handler"));

  // sym2: Qualname exact
  auto f2 = make_facts("worker.py");
  f2.symbols.push_back(make_sym("worker.py::Task.handler", "handler", "Task.handler", SymbolKind::Method));

  // sym3: ID does not match, qualname does not match, name matches
  auto f3 = make_facts("client.py");
  f3.symbols.push_back(make_sym("client.py::Client.do_handler", "handler", "Client.do_handler", SymbolKind::Method));

  store.put(std::move(f1));
  store.put(std::move(f2));
  store.put(std::move(f3));

  SUBCASE("Exact ID lookup takes top priority") {
    auto res = store.find_symbols("service.py::handler", 10);
    REQUIRE(!res.empty());
    CHECK(res[0]->id == "service.py::handler");
  }

  SUBCASE("Qualname query prioritizes Task.handler over other symbols") {
    auto res = store.find_symbols("Task.handler", 10);
    REQUIRE(res.size() == 1);
    CHECK(res[0]->id == "worker.py::Task.handler");
  }

  SUBCASE("Name query returns qualname matches before simple name matches") {
    auto res = store.find_symbols("handler", 10);
    REQUIRE(res.size() == 3);
    CHECK(res[0]->id == "service.py::handler");
  }

  SUBCASE("Limit truncation works accurately") {
    auto res = store.find_symbols("handler", 2);
    CHECK(res.size() == 2);
  }
}

TEST_CASE("FactStore staleness check (is_stale)") {
  auto temp_dir = std::filesystem::temp_directory_path() / "gs_test_fact_store_stale";
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  std::filesystem::create_directories(temp_dir, ec);

  auto file_path = temp_dir / "mod.py";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    ofs << "x = 42\n";
  }

  auto st = platform::stat(file_path);
  FactStore store(temp_dir);

  auto facts = make_facts("mod.py", st.size, st.mtime_ns);
  store.put(std::move(facts));

  CHECK_FALSE(store.is_stale("mod.py"));

  SUBCASE("Appending content marks file stale") {
    {
      std::ofstream ofs(file_path, std::ios::app | std::ios::binary);
      ofs << "y = 100\n";
    }
    CHECK(store.is_stale("mod.py"));
  }

  SUBCASE("Deleting file marks file stale") {
    std::filesystem::remove(file_path, ec);
    CHECK(store.is_stale("mod.py"));
  }

  SUBCASE("Unindexed file returns stale") {
    CHECK(store.is_stale("unknown.py"));
  }

  std::filesystem::remove_all(temp_dir, ec);
}

TEST_CASE("FactStore multi-threaded concurrent access stress test") {
  FactStore store;

  // Pre-populate with 20 files
  for (int i = 0; i < 20; ++i) {
    auto f = make_facts("file_" + std::to_string(i) + ".py");
    for (int j = 0; j < 5; ++j) {
      f.symbols.push_back(make_sym("file_" + std::to_string(i) + ".py::sym_" + std::to_string(j),
                                   "sym_" + std::to_string(j),
                                   "sym_" + std::to_string(j)));
    }
    store.put(std::move(f));
  }

  std::vector<std::thread> readers;
  std::vector<std::thread> writers;
  std::atomic<bool> stop{false};

  // 4 Reader threads
  for (int t = 0; t < 4; ++t) {
    readers.emplace_back([&store, &stop]() {
      while (!stop.load()) {
        auto syms = store.find_symbols("sym_2", 10);
        (void)syms;
        const auto* s = store.find_symbol_by_id("file_1.py::sym_1");
        (void)s;
        auto files = store.all_files();
        (void)files;
      }
    });
  }

  // 2 Writer threads
  for (int t = 0; t < 2; ++t) {
    writers.emplace_back([&store, &stop, t]() {
      int iter = 0;
      while (!stop.load() && iter < 100) {
        std::string fname = "concurrent_" + std::to_string(t) + ".py";
        auto f = make_facts(fname, 100 + iter, 1000 + iter);
        f.symbols.push_back(make_sym(fname + "::fn", "fn", "fn"));
        store.put(std::move(f));
        if (iter % 10 == 0) {
          store.erase(fname);
        }
        ++iter;
      }
    });
  }

  for (auto& w : writers) w.join();
  stop.store(true);
  for (auto& r : readers) r.join();

  CHECK(store.file_count() >= 20);
}
