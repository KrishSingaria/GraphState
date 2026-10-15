#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "core/config.hpp"
#include "core/paths.hpp"
#include "index/fact_store.hpp"
#include "index/resolver.hpp"
#include "index/scanner.hpp"
#include "platform/platform.hpp"

using namespace gs;
using namespace gs::index;
using namespace gs::lang;

namespace {

struct TempDirectory {
  std::filesystem::path path;

  explicit TempDirectory(std::string_view name) {
    path = std::filesystem::temp_directory_path() / ("gs_adv_" + std::string(name));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }

  ~TempDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  void write_file(std::string_view rel, std::string_view content) {
    auto p = path / platform::from_utf8(rel);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  }

  void write_binary_file(std::string_view rel, size_t size, size_t nul_offset) {
    auto p = path / platform::from_utf8(rel);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary);
    for (size_t i = 0; i < size; ++i) {
      char c = (i == nul_offset) ? '\0' : 'A';
      ofs.put(c);
    }
  }
};

SymbolFact make_adv_sym(std::string id, std::string name, std::string qualname, SymbolKind kind = SymbolKind::Function) {
  SymbolFact s;
  s.id = std::move(id);
  s.name = std::move(name);
  s.qualname = std::move(qualname);
  s.kind = kind;
  return s;
}

FileFacts make_adv_facts(std::string file, uint64_t size = 100, int64_t mtime = 1000) {
  FileFacts f;
  f.file = std::move(file);
  f.lang = Language::Python;
  f.size = size;
  f.mtime_ns = mtime;
  f.hash = "deadbeef";
  return f;
}

} // namespace

// ============================================================================
// FOCUS 1: FactStore Concurrency Stress, Deadlocks & Boundary Behavior
// ============================================================================

TEST_CASE("Adversarial FactStore: Rapid Concurrent Read-Write Races") {
  FactStore store;

  // Pre-seed with 50 files, 10 symbols each
  for (int i = 0; i < 50; ++i) {
    auto f = make_adv_facts("seed_" + std::to_string(i) + ".py");
    for (int j = 0; j < 10; ++j) {
      std::string sname = "sym_" + std::to_string(j);
      f.symbols.push_back(make_adv_sym(f.file + "::" + sname, sname, sname));
    }
    store.put(std::move(f));
  }

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> read_ops{0};
  std::atomic<uint64_t> write_ops{0};

  // 4 Readers
  std::vector<std::thread> readers;
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&]() {
      while (!stop.load(std::memory_order_relaxed)) {
        // Query symbols
        auto res = store.find_symbols("sym_5", 10);
        (void)res;
        const auto* s = store.find_symbol_by_id("seed_10.py::sym_5");
        (void)s;
        auto by_name = store.find_symbols_by_name("sym_2");
        (void)by_name;
        auto by_qual = store.find_symbols_by_qualname("sym_3");
        (void)by_qual;
        auto files = store.all_files();
        (void)files;
        size_t total = store.total_symbols();
        (void)total;
        read_ops.fetch_add(6, std::memory_order_relaxed);
      }
    });
  }

  // 2 Writers continuously putting and erasing
  std::vector<std::thread> writers;
  for (int w = 0; w < 2; ++w) {
    writers.emplace_back([&, w]() {
      int iter = 0;
      while (!stop.load(std::memory_order_relaxed) && iter < 1000) {
        std::string fname = "dyn_" + std::to_string(w) + "_" + std::to_string(iter % 10) + ".py";
        auto f = make_adv_facts(fname, 100 + iter, 2000 + iter);
        for (int j = 0; j < 5; ++j) {
          f.symbols.push_back(make_adv_sym(fname + "::fn_" + std::to_string(j),
                                           "fn_" + std::to_string(j),
                                           "fn_" + std::to_string(j)));
        }
        store.put(std::move(f));
        write_ops.fetch_add(1, std::memory_order_relaxed);

        if (iter % 3 == 0) {
          store.erase(fname);
          write_ops.fetch_add(1, std::memory_order_relaxed);
        }
        ++iter;
      }
    });
  }

  for (auto& wr : writers) wr.join();
  stop.store(true, std::memory_order_relaxed);
  for (auto& rd : readers) rd.join();

  CHECK(read_ops.load() > 1000);
  CHECK(write_ops.load() >= 2000);
  CHECK(store.file_count() >= 50);
}

TEST_CASE("Adversarial FactStore: Double put() Race on Same File with Size Asymmetry") {
  FactStore store;

  // Two threads hammering put() on the exact same file
  std::atomic<bool> start_signal{false};
  constexpr int ITERS = 500;

  std::thread t1([&]() {
    while (!start_signal.load(std::memory_order_relaxed)) {}
    for (int i = 0; i < ITERS; ++i) {
      auto f = make_adv_facts("race_file.py", 100, 1000 + i);
      // Thread 1 puts 10 symbols
      for (int s = 0; s < 10; ++s) {
        f.symbols.push_back(make_adv_sym("race_file.py::sym_" + std::to_string(s),
                                         "sym_" + std::to_string(s),
                                         "sym_" + std::to_string(s)));
      }
      store.put(std::move(f));
    }
  });

  std::thread t2([&]() {
    while (!start_signal.load(std::memory_order_relaxed)) {}
    for (int i = 0; i < ITERS; ++i) {
      auto f = make_adv_facts("race_file.py", 200, 2000 + i);
      // Thread 2 puts 3 symbols
      for (int s = 0; s < 3; ++s) {
        f.symbols.push_back(make_adv_sym("race_file.py::sym_" + std::to_string(s),
                                         "sym_" + std::to_string(s),
                                         "sym_" + std::to_string(s)));
      }
      store.put(std::move(f));
    }
  });

  start_signal.store(true, std::memory_order_relaxed);
  t1.join();
  t2.join();

  // Verify consistency after race finishes
  const auto* facts = store.get("race_file.py");
  REQUIRE(facts != nullptr);
  size_t expected_symbols = facts->symbols.size();
  CHECK((expected_symbols == 10 || expected_symbols == 3));
  CHECK(store.total_symbols() == expected_symbols);

  // Check multimap consistency
  for (size_t s = 0; s < expected_symbols; ++s) {
    std::string sname = "sym_" + std::to_string(s);
    auto matches = store.find_symbols_by_name(sname);
    CHECK(matches.size() == 1);
  }
}

TEST_CASE("Adversarial FactStore: Rapid erase() while Readers Call find_symbols()") {
  FactStore store;

  // Put a file with 20 symbols
  auto populate = [&]() {
    auto f = make_adv_facts("target_file.py");
    for (int i = 0; i < 20; ++i) {
      f.symbols.push_back(make_adv_sym("target_file.py::sym_" + std::to_string(i),
                                       "sym_" + std::to_string(i),
                                       "sym_" + std::to_string(i)));
    }
    store.put(std::move(f));
  };
  populate();

  std::atomic<bool> stop{false};

  // Reader thread repeatedly queries
  std::thread reader([&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      auto syms = store.find_symbols("sym_5", 5);
      const auto* s = store.find_symbol_by_id("target_file.py::sym_5");
      // If found, reading s inside the scope or immediately shouldn't crash
      if (s) {
        CHECK(s->name.starts_with("sym_"));
      }
    }
  });

  // Writer thread repeatedly populates and erases
  std::thread writer([&]() {
    for (int i = 0; i < 500; ++i) {
      store.erase("target_file.py");
      populate();
    }
    stop.store(true, std::memory_order_relaxed);
  });

  writer.join();
  reader.join();
}

TEST_CASE("Adversarial FactStore: Case-Insensitive put() Mismatch Investigation") {
  FactStore store;
  store.set_case_insensitive(true);

  // Put file with mixed casing
  auto f1 = make_adv_facts("src/App.py");
  f1.symbols.push_back(make_adv_sym("src/App.py::sym1", "sym1", "sym1"));
  f1.symbols.push_back(make_adv_sym("src/App.py::sym2", "sym2", "sym2"));
  store.put(std::move(f1));

  CHECK(store.file_count() == 1);
  CHECK(store.total_symbols() == 2);

  // Now put replacement file with lowercase casing
  auto f2 = make_adv_facts("src/app.py");
  f2.symbols.push_back(make_adv_sym("src/app.py::sym3", "sym3", "sym3"));
  store.put(std::move(f2));

  // Does store have 1 file or 2 files?
  // In a case-insensitive store, "src/App.py" and "src/app.py" represent the SAME file on disk.
  // If file_count is 2, it is a data corruption bug in case-insensitive handling!
  INFO("store.file_count() = " << store.file_count());
  INFO("store.total_symbols() = " << store.total_symbols());
  CHECK(store.file_count() == 1);
  CHECK(store.total_symbols() == 1);
}

// ============================================================================
// FOCUS 2: Scanner Boundary Conditions
// ============================================================================

TEST_CASE("Adversarial Scanner: NUL Byte at Exact Byte 8191 vs 8192") {
  TempDirectory tmp("scanner_nul_boundary");

  // Case A: File size 8192 bytes, NUL at byte 8191 (offset 8191 is the 8192nd byte)
  // This is within the first 8 KiB window (bytes [0..8191]). Must be detected as binary.
  tmp.write_binary_file("nul_at_8191.bin", 8192, 8191);
  CHECK(is_binary_file(tmp.path / "nul_at_8191.bin") == true);

  // Case B: File size 8193 bytes, NUL at byte 8192 (offset 8192 is the 8193rd byte)
  // This is in the 9th KiB, outside the first 8 KiB window. Must NOT be detected as binary.
  tmp.write_binary_file("nul_at_8192.txt", 8193, 8192);
  CHECK(is_binary_file(tmp.path / "nul_at_8192.txt") == false);

  // Case C: File size exactly 8192 bytes with NO NUL byte
  tmp.write_file("no_nul_8192.txt", std::string(8192, 'A'));
  CHECK(is_binary_file(tmp.path / "no_nul_8192.txt") == false);

  // Case D: File size 8191 bytes with NUL at byte 8190
  tmp.write_binary_file("nul_at_8190.bin", 8191, 8190);
  CHECK(is_binary_file(tmp.path / "nul_at_8190.bin") == true);
}

TEST_CASE("Adversarial Scanner: 0-Byte File Handling") {
  TempDirectory tmp("scanner_zero_byte");
  tmp.write_file("empty.py", "");

  CHECK(is_binary_file(tmp.path / "empty.py") == false);

  core::Config config;
  config.root = tmp.path;
  config.index.use_git_ls_files = false; // Fallback walk

  auto scanned = scan(config, tmp.path);
  REQUIRE(scanned.size() == 1);
  CHECK(scanned[0].rel == "empty.py");
  CHECK(scanned[0].size == 0);
}

TEST_CASE("Adversarial Scanner: Huge Files & Exact max_file_bytes Threshold") {
  TempDirectory tmp("scanner_huge_files");

  constexpr uint64_t MAX_LIMIT = 5000;
  core::Config config;
  config.root = tmp.path;
  config.index.use_git_ls_files = false;
  config.index.max_file_bytes = MAX_LIMIT;

  // File 1: Exactly at threshold (5000 bytes) -> Must be kept
  tmp.write_file("exact_limit.py", std::string(MAX_LIMIT, 'X'));

  // File 2: 1 byte above threshold (5001 bytes) -> Must be dropped
  tmp.write_file("above_limit.py", std::string(MAX_LIMIT + 1, 'Y'));

  // File 3: Large file (500,000 bytes) -> Must be dropped
  tmp.write_file("huge.py", std::string(500000, 'Z'));

  auto scanned = scan(config, tmp.path);
  REQUIRE(scanned.size() == 1);
  CHECK(scanned[0].rel == "exact_limit.py");
  CHECK(scanned[0].size == MAX_LIMIT);
}

TEST_CASE("Adversarial Scanner: Hidden Files and Non-Git Directory Walk") {
  TempDirectory tmp("scanner_hidden_nongit");

  // Non-git directory: verify fallback walk works seamlessly
  tmp.write_file(".env", "DATABASE_URL=sqlite://\n");
  tmp.write_file(".hidden.py", "secret = True\n");
  tmp.write_file("normal.py", "x = 1\n");
  tmp.write_file(".github/workflows/ci.yml", "name: CI\n");

  core::Config config;
  config.root = tmp.path;
  config.index.use_git_ls_files = true; // Git is requested, but directory is not a git repo
  config.index.ignore = {"**/.git/**"};

  auto scanned = scan(config, tmp.path);

  std::vector<std::string> paths;
  for (const auto& s : scanned) paths.push_back(s.rel);

  CHECK(std::find(paths.begin(), paths.end(), "normal.py") != paths.end());
  CHECK(std::find(paths.begin(), paths.end(), ".env") != paths.end());
  CHECK(std::find(paths.begin(), paths.end(), ".hidden.py") != paths.end());
  CHECK(std::find(paths.begin(), paths.end(), ".github/workflows/ci.yml") != paths.end());
}

// ============================================================================
// FOCUS 3: Resolver Edge Cases
// ============================================================================

TEST_CASE("Adversarial Resolver: Identical Target Names Across Multiple Files (Heuristic Step 8)") {
  FactStore store;

  // File 1 defines calculate_shipping
  auto f1 = make_adv_facts("shipping_us.py");
  f1.symbols.push_back(make_adv_sym("shipping_us.py::calculate_shipping", "calculate_shipping", "calculate_shipping"));
  store.put(std::move(f1));

  // File 2 also defines calculate_shipping
  auto f2 = make_adv_facts("shipping_eu.py");
  f2.symbols.push_back(make_adv_sym("shipping_eu.py::calculate_shipping", "calculate_shipping", "calculate_shipping"));
  store.put(std::move(f2));

  // File 3 calls courier.calculate_shipping()
  auto f3 = make_adv_facts("checkout.py");
  f3.symbols.push_back(make_adv_sym("checkout.py::process", "process", "process"));
  CallFact call;
  call.caller_id = "checkout.py::process";
  call.shape = CalleeShape::Attr;
  call.receiver = "courier";
  call.name = "calculate_shipping";
  f3.calls.push_back(call);
  store.put(std::move(f3));

  Resolver resolver;
  resolver.rebuild_indices(store);

  auto resolved = resolver.resolve_file(store, "checkout.py");

  // Step 8 is a SINGLE-match heuristic. Since 2 matches exist, it MUST NOT resolve!
  CHECK(resolved.empty());
}

TEST_CASE("Adversarial Resolver: Collision with COMMON_NAMES Tokens") {
  FactStore store;

  // File 1 defines standalone utility functions whose names are in COMMON_NAMES
  auto utils = make_adv_facts("utils.py");
  utils.symbols.push_back(make_adv_sym("utils.py::format", "format", "format"));
  utils.symbols.push_back(make_adv_sym("utils.py::run", "run", "run"));
  utils.symbols.push_back(make_adv_sym("utils.py::get", "get", "get"));
  utils.symbols.push_back(make_adv_sym("utils.py::execute", "execute", "execute"));
  store.put(std::move(utils));

  // File 2 calls obj.format(), runner.run(), dict.get(), task.execute()
  auto caller_file = make_adv_facts("caller.py");
  caller_file.symbols.push_back(make_adv_sym("caller.py::do_all", "do_all", "do_all"));

  for (const auto& token : {"format", "run", "get", "execute"}) {
    CallFact c;
    c.caller_id = "caller.py::do_all";
    c.shape = CalleeShape::Attr;
    c.receiver = "obj";
    c.name = token;
    caller_file.calls.push_back(c);
  }
  store.put(std::move(caller_file));

  Resolver resolver;
  resolver.rebuild_indices(store);

  auto resolved = resolver.resolve_file(store, "caller.py");

  // None of these should be resolved via heuristic Step 8 because they are in COMMON_NAMES
  CHECK(resolved.empty());
}

TEST_CASE("Adversarial Resolver: Circular Imports Between Modules") {
  FactStore store;

  // mod_a.py imports mod_b
  auto mod_a = make_adv_facts("mod_a.py");
  mod_a.symbols.push_back(make_adv_sym("mod_a.py::fn_a", "fn_a", "fn_a"));
  ImportFact imp_b;
  imp_b.kind = ImportFact::FromNames;
  imp_b.spec = "mod_b";
  imp_b.names = {{"fn_b", "fn_b"}};
  mod_a.imports.push_back(imp_b);

  CallFact call_b;
  call_b.caller_id = "mod_a.py::fn_a";
  call_b.shape = CalleeShape::Name;
  call_b.name = "fn_b";
  mod_a.calls.push_back(call_b);

  // mod_b.py imports mod_a
  auto mod_b = make_adv_facts("mod_b.py");
  mod_b.symbols.push_back(make_adv_sym("mod_b.py::fn_b", "fn_b", "fn_b"));
  ImportFact imp_a;
  imp_a.kind = ImportFact::FromNames;
  imp_a.spec = "mod_a";
  imp_a.names = {{"fn_a", "fn_a"}};
  mod_b.imports.push_back(imp_a);

  CallFact call_a;
  call_a.caller_id = "mod_b.py::fn_b";
  call_a.shape = CalleeShape::Name;
  call_a.name = "fn_a";
  mod_b.calls.push_back(call_a);

  store.put(std::move(mod_a));
  store.put(std::move(mod_b));

  Resolver resolver;

  // Dependencies update
  resolver.update_dependencies("mod_a.py", {imp_b}, store);
  resolver.update_dependencies("mod_b.py", {imp_a}, store);

  auto deps_a = resolver.dependents_of("mod_a.py");
  auto deps_b = resolver.dependents_of("mod_b.py");

  REQUIRE(deps_a.size() == 1);
  CHECK(deps_a[0] == "mod_b.py");
  REQUIRE(deps_b.size() == 1);
  CHECK(deps_b[0] == "mod_a.py");

  // Both files resolve cleanly without infinite loops
  auto res_a = resolver.resolve_file(store, "mod_a.py");
  REQUIRE(res_a.size() == 1);
  CHECK(res_a[0].target_id == "mod_b.py::fn_b");

  auto res_b = resolver.resolve_file(store, "mod_b.py");
  REQUIRE(res_b.size() == 1);
  CHECK(res_b[0].target_id == "mod_a.py::fn_a");
}

TEST_CASE("Adversarial Resolver: Deep Inheritance Chain Resolution") {
  FactStore store;

  // Class hierarchy: Base -> Intermediate -> Leaf
  auto file = make_adv_facts("hierarchy.py");

  // Base defines base_action and __init__
  file.symbols.push_back(make_adv_sym("hierarchy.py::Base", "Base", "Base", SymbolKind::Class));
  file.symbols.push_back(make_adv_sym("hierarchy.py::Base.__init__", "__init__", "Base.__init__", SymbolKind::Constructor));
  file.symbols.push_back(make_adv_sym("hierarchy.py::Base.base_action", "base_action", "Base.base_action", SymbolKind::Method));

  // Intermediate inherits from Base
  auto inter = make_adv_sym("hierarchy.py::Intermediate", "Intermediate", "Intermediate", SymbolKind::Class);
  inter.bases = {"Base"};
  file.symbols.push_back(inter);
  file.symbols.push_back(make_adv_sym("hierarchy.py::Intermediate.inter_action", "inter_action", "Intermediate.inter_action", SymbolKind::Method));

  // Leaf inherits from Intermediate
  auto leaf = make_adv_sym("hierarchy.py::Leaf", "Leaf", "Leaf", SymbolKind::Class);
  leaf.bases = {"Intermediate"};
  file.symbols.push_back(leaf);

  auto leaf_work = make_adv_sym("hierarchy.py::Leaf.work", "work", "Leaf.work", SymbolKind::Method);
  leaf_work.class_qualname = "Leaf";
  file.symbols.push_back(leaf_work);

  // Call 1: self.base_action() inside Leaf.work
  CallFact c1;
  c1.caller_id = "hierarchy.py::Leaf.work";
  c1.shape = CalleeShape::SelfAttr;
  c1.name = "base_action";
  file.calls.push_back(c1);

  // Call 2: Leaf() constructor lookup (should resolve to Base.__init__)
  CallFact c2;
  c2.caller_id = "hierarchy.py::Leaf.work";
  c2.shape = CalleeShape::Name;
  c2.name = "Leaf";
  file.calls.push_back(c2);

  store.put(std::move(file));

  Resolver resolver;
  auto resolved = resolver.resolve_file(store, "hierarchy.py");

  auto find_target = [&](std::string_view tid) -> const ResolvedCall* {
    for (const auto& r : resolved) {
      if (r.target_id == tid) return &r;
    }
    return nullptr;
  };

  const auto* r1 = find_target("hierarchy.py::Base.base_action");
  REQUIRE(r1 != nullptr);
  CHECK(r1->confidence == "exact");
  CHECK(r1->call_kind == "instance");

  const auto* r2 = find_target("hierarchy.py::Base.__init__");
  REQUIRE(r2 != nullptr);
  CHECK(r2->confidence == "exact");
  CHECK(r2->call_kind == "constructor");
}

TEST_CASE("Adversarial Resolver: Python Relative Import with Single Dot 'from . import sibling'") {
  FactStore store;

  // Setup package: pkg/__init__.py, pkg/app.py, pkg/utils.py
  store.put(make_adv_facts("pkg/__init__.py"));

  auto utils = make_adv_facts("pkg/utils.py");
  utils.symbols.push_back(make_adv_sym("pkg/utils.py::helper", "helper", "helper"));
  store.put(std::move(utils));

  auto app = make_adv_facts("pkg/app.py");
  app.symbols.push_back(make_adv_sym("pkg/app.py::run", "run", "run"));

  ImportFact imp;
  imp.kind = ImportFact::FromNames;
  imp.spec = ".";  // from . import utils
  imp.names = {{"utils", "utils"}};
  app.imports.push_back(imp);

  CallFact call;
  call.caller_id = "pkg/app.py::run";
  call.shape = CalleeShape::Attr;
  call.receiver = "utils";
  call.name = "helper";
  app.calls.push_back(call);

  store.put(std::move(app));

  Resolver resolver;
  auto resolved_imports = resolver.resolve_imports(store, "pkg/app.py");

  // Check where 'from . import utils' resolves
  REQUIRE(!resolved_imports.empty());
  INFO("Resolved import target: " << resolved_imports[0].target_file);
  CHECK(resolved_imports[0].target_file == "pkg/utils.py");

  // Verify call resolution for imported sibling submodule (Step 5A)
  auto calls = resolver.resolve_file(store, "pkg/app.py");
  REQUIRE(!calls.empty());
  CHECK(calls[0].target_id == "pkg/utils.py::helper");
  CHECK(calls[0].confidence == "exact");
  CHECK(calls[0].call_kind == "plain");

  // Verify reverse dependency tracking
  resolver.update_dependencies("pkg/app.py", store.get("pkg/app.py")->imports, store);
  auto deps = resolver.dependents_of("pkg/utils.py");
  CHECK(std::find(deps.begin(), deps.end(), "pkg/app.py") != deps.end());
}

TEST_CASE("Adversarial Resolver: Python Relative Import with Double Dot 'from .. import sibling'") {
  FactStore store;
  store.put(make_adv_facts("pkg/__init__.py"));
  store.put(make_adv_facts("pkg/utils.py"));
  store.put(make_adv_facts("pkg/sub/__init__.py"));
  auto sub_app = make_adv_facts("pkg/sub/app.py");
  ImportFact imp;
  imp.kind = ImportFact::FromNames;
  imp.spec = "..";
  imp.names = {{"utils", "utils"}};
  sub_app.imports.push_back(imp);
  store.put(std::move(sub_app));

  Resolver resolver;
  auto resolved = resolver.resolve_imports(store, "pkg/sub/app.py");
  REQUIRE(!resolved.empty());
  CHECK(resolved[0].target_file == "pkg/utils.py");
  CHECK(resolved[0].whole_module == true);
}

TEST_CASE("Adversarial Resolver: Dotted Base Class Across Modules") {
  FactStore store;

  // base_mod.py
  auto base_mod = make_adv_facts("base_mod.py");
  base_mod.symbols.push_back(make_adv_sym("base_mod.py::BaseClass", "BaseClass", "BaseClass", SymbolKind::Class));
  base_mod.symbols.push_back(make_adv_sym("base_mod.py::BaseClass.base_action", "base_action", "BaseClass.base_action", SymbolKind::Method));
  store.put(std::move(base_mod));

  // child_mod.py imports base_mod as module
  auto child_mod = make_adv_facts("child_mod.py");
  ImportFact imp;
  imp.kind = ImportFact::Module;
  imp.spec = "base_mod";
  child_mod.imports.push_back(imp);

  auto child_cls = make_adv_sym("child_mod.py::ChildClass", "ChildClass", "ChildClass", SymbolKind::Class);
  child_cls.bases = {"base_mod.BaseClass"};
  child_mod.symbols.push_back(child_cls);

  auto child_method = make_adv_sym("child_mod.py::ChildClass.do_something", "do_something", "ChildClass.do_something", SymbolKind::Method);
  child_method.class_qualname = "ChildClass";
  child_mod.symbols.push_back(child_method);

  // Call self.base_action() inside ChildClass.do_something
  CallFact c1;
  c1.caller_id = "child_mod.py::ChildClass.do_something";
  c1.shape = CalleeShape::SelfAttr;
  c1.name = "base_action";
  child_mod.calls.push_back(c1);

  store.put(std::move(child_mod));

  Resolver resolver;
  auto resolved = resolver.resolve_file(store, "child_mod.py");

  REQUIRE(!resolved.empty());
  CHECK(resolved[0].target_id == "base_mod.py::BaseClass.base_action");
}

TEST_CASE("Adversarial Resolver: Circular Class Inheritance Cycle") {
  FactStore store;

  auto file = make_adv_facts("cycle.py");

  // Class A inherits from B
  auto cls_a = make_adv_sym("cycle.py::ClassA", "ClassA", "ClassA", SymbolKind::Class);
  cls_a.bases = {"ClassB"};
  file.symbols.push_back(cls_a);

  // Class B inherits from A
  auto cls_b = make_adv_sym("cycle.py::ClassB", "ClassB", "ClassB", SymbolKind::Class);
  cls_b.bases = {"ClassA"};
  file.symbols.push_back(cls_b);

  auto method_a = make_adv_sym("cycle.py::ClassA.foo", "foo", "ClassA.foo", SymbolKind::Method);
  method_a.class_qualname = "ClassA";
  file.symbols.push_back(method_a);

  // Call missing method non_existent on self inside ClassA.foo
  CallFact c1;
  c1.caller_id = "cycle.py::ClassA.foo";
  c1.shape = CalleeShape::SelfAttr;
  c1.name = "non_existent";
  file.calls.push_back(c1);

  store.put(std::move(file));

  Resolver resolver;
  std::atomic<bool> done{false};
  std::thread runner([&]() {
    auto res = resolver.resolve_file(store, "cycle.py");
    (void)res;
    done.store(true);
  });

  auto start = std::chrono::steady_clock::now();
  while (!done.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  bool finished = done.load();
  if (finished) {
    runner.join();
  } else {
    runner.detach();
  }
  CHECK(finished);
}

