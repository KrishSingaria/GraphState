#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <algorithm>
#include <vector>
#include "index/resolver.hpp"
#include "index/fact_store.hpp"
#include "core/config.hpp"

using namespace gs;
using namespace gs::index;
using namespace gs::lang;

namespace {

SymbolFact make_func(std::string file, std::string name, std::string qualname = "") {
  if (qualname.empty()) qualname = name;
  SymbolFact s;
  s.id = file + "::" + qualname;
  s.name = name;
  s.qualname = qualname;
  s.kind = SymbolKind::Function;
  return s;
}

SymbolFact make_method(std::string file, std::string cls, std::string name, bool is_static = false, std::vector<std::string> decs = {}) {
  SymbolFact s;
  s.id = file + "::" + cls + "." + name;
  s.name = name;
  s.qualname = cls + "." + name;
  s.class_qualname = cls;
  s.kind = (name == "__init__" || name == "constructor") ? SymbolKind::Constructor : SymbolKind::Method;
  s.is_static = is_static;
  s.decorators = std::move(decs);
  return s;
}

SymbolFact make_class(std::string file, std::string name, std::vector<std::string> bases = {}) {
  SymbolFact s;
  s.id = file + "::" + name;
  s.name = name;
  s.qualname = name;
  s.kind = SymbolKind::Class;
  s.bases = std::move(bases);
  return s;
}

FileFacts make_empty_facts(std::string file, Language l = Language::Python) {
  FileFacts f;
  f.file = std::move(file);
  f.lang = l;
  f.hash = "abcdef0123456789";
  f.size = 100;
  f.mtime_ns = 1000;
  return f;
}

} // namespace

TEST_CASE("Python Module Resolution") {
  FactStore store;
  store.put(make_empty_facts("billing.py"));
  store.put(make_empty_facts("pkg/__init__.py"));
  store.put(make_empty_facts("pkg/billing.py"));
  store.put(make_empty_facts("pkg/utils.py"));
  store.put(make_empty_facts("src/math_pkg/core.py"));

  Resolver resolver(std::vector<std::string>{"", "src"});
  resolver.set_python_source_roots({"", "src"});

  SUBCASE("Absolute root module") {
    CHECK(resolver.resolve_python_module(store, "checkout.py", "billing") == "billing.py");
  }

  SUBCASE("Absolute source root module") {
    CHECK(resolver.resolve_python_module(store, "checkout.py", "math_pkg.core") == "src/math_pkg/core.py");
  }

  SUBCASE("Package init module") {
    CHECK(resolver.resolve_python_module(store, "app.py", "pkg") == "pkg/__init__.py");
  }

  SUBCASE("Relative single dot") {
    CHECK(resolver.resolve_python_module(store, "pkg/app.py", ".billing") == "pkg/billing.py");
    CHECK(resolver.resolve_python_module(store, "pkg/app.py", ".") == "pkg/__init__.py");
  }

  SUBCASE("Relative double dot traversal") {
    CHECK(resolver.resolve_python_module(store, "pkg/sub/app.py", "..utils") == "pkg/utils.py");
  }

  SUBCASE("Relative escape workspace fails") {
    CHECK(resolver.resolve_python_module(store, "pkg/app.py", "....bad") == "");
  }

  SUBCASE("External library resolves to empty") {
    CHECK(resolver.resolve_python_module(store, "billing.py", "decimal") == "");
    CHECK(resolver.resolve_python_module(store, "billing.py", "os") == "");
  }
}

TEST_CASE("TypeScript Module Resolution") {
  FactStore store;
  store.put(make_empty_facts("web/api.ts", Language::TypeScript));
  store.put(make_empty_facts("web/cart.ts", Language::TypeScript));
  store.put(make_empty_facts("components/Button.tsx", Language::TypeScript));
  store.put(make_empty_facts("lib/helper/index.ts", Language::TypeScript));

  Resolver resolver;

  SUBCASE("Relative resolution with extension probing") {
    CHECK(resolver.resolve_ts_module(store, "web/cart.ts", "./api") == "web/api.ts");
    CHECK(resolver.resolve_ts_module(store, "web/cart.ts", "../components/Button") == "components/Button.tsx");
    CHECK(resolver.resolve_ts_module(store, "web/cart.ts", "../lib/helper") == "lib/helper/index.ts");
  }

  SUBCASE("ESM-in-TS remapping") {
    CHECK(resolver.resolve_ts_module(store, "web/index.ts", "./cart.js") == "web/cart.ts");
  }

  SUBCASE("Bare specifier resolves to empty (external)") {
    CHECK(resolver.resolve_ts_module(store, "web/cart.ts", "react") == "");
    CHECK(resolver.resolve_ts_module(store, "web/cart.ts", "@scope/package") == "");
  }
}

TEST_CASE("8-Step Call Resolution Algorithm") {
  FactStore store;

  // Setup file: app.py
  auto app = make_empty_facts("app.py", Language::Python);
  app.module_names = {"compute", "main"};

  // Caller: main with local closure 'helper'
  auto main_fn = make_func("app.py", "main");
  main_fn.locals = {"helper", "x"};
  app.symbols.push_back(main_fn);

  // Module function: compute
  app.symbols.push_back(make_func("app.py", "compute"));

  // Classes & Methods
  app.symbols.push_back(make_class("app.py", "Base"));
  app.symbols.push_back(make_method("app.py", "Base", "__init__"));

  app.symbols.push_back(make_class("app.py", "Worker", {"Base"}));
  app.symbols.push_back(make_method("app.py", "Worker", "__init__"));
  app.symbols.push_back(make_method("app.py", "Worker", "do_work"));

  app.symbols.push_back(make_class("app.py", "MathUtil"));
  app.symbols.push_back(make_method("app.py", "MathUtil", "add", true));

  // Imports: from billing import calculate_total
  ImportFact imp;
  imp.kind = ImportFact::FromNames;
  imp.spec = "billing";
  imp.names = {{"calculate_total", "calculate_total"}};
  app.imports.push_back(imp);

  // Calls from app.py::main
  // Call 1: helper() -> local closure
  CallFact c1;
  c1.caller_id = "app.py::main";
  c1.shape = CalleeShape::Name;
  c1.name = "helper";
  c1.line = 10;
  app.calls.push_back(c1);

  // Call 2: compute() -> local module function (Step 2)
  CallFact c2;
  c2.caller_id = "app.py::main";
  c2.shape = CalleeShape::Name;
  c2.name = "compute";
  c2.line = 11;
  app.calls.push_back(c2);

  // Call 3: Worker() -> constructor (Step 3)
  CallFact c3;
  c3.caller_id = "app.py::main";
  c3.shape = CalleeShape::Name;
  c3.name = "Worker";
  c3.line = 12;
  app.calls.push_back(c3);

  // Call 4: calculate_total() -> imported symbol (Step 4)
  CallFact c4;
  c4.caller_id = "app.py::main";
  c4.shape = CalleeShape::Name;
  c4.name = "calculate_total";
  c4.line = 13;
  app.calls.push_back(c4);

  // Call 5B: MathUtil.add() -> static method (Step 5B)
  CallFact c5;
  c5.caller_id = "app.py::main";
  c5.shape = CalleeShape::Attr;
  c5.receiver = "MathUtil";
  c5.name = "add";
  c5.line = 14;
  app.calls.push_back(c5);

  // Call 6: self.do_work() inside Worker.__init__ (Step 6)
  CallFact c6;
  c6.caller_id = "app.py::Worker.__init__";
  c6.shape = CalleeShape::SelfAttr;
  c6.name = "do_work";
  c6.line = 20;
  app.calls.push_back(c6);

  // Call 7: super().__init__() inside Worker.__init__ (Step 7)
  CallFact c7;
  c7.caller_id = "app.py::Worker.__init__";
  c7.shape = CalleeShape::SuperAttr;
  c7.name = "__init__";
  c7.line = 21;
  app.calls.push_back(c7);

  // Call 8: client.send_payload() -> heuristic (Step 8)
  CallFact c8;
  c8.caller_id = "app.py::main";
  c8.shape = CalleeShape::Attr;
  c8.receiver = "client";
  c8.name = "send_payload";
  c8.line = 25;
  app.calls.push_back(c8);

  // Setup file: billing.py
  auto billing = make_empty_facts("billing.py", Language::Python);
  billing.symbols.push_back(make_func("billing.py", "calculate_total"));

  // Setup file: network.py with unique send_payload
  auto network = make_empty_facts("network.py", Language::Python);
  network.symbols.push_back(make_func("network.py", "send_payload"));

  store.put(std::move(app));
  store.put(std::move(billing));
  store.put(std::move(network));

  Resolver resolver;
  resolver.rebuild_indices(store);

  auto resolved = resolver.resolve_file(store, "app.py");

  // Verify Call 1: helper() closure was skipped (0 edges)
  auto find_target = [&](std::string_view tid) -> const ResolvedCall* {
    for (const auto& r : resolved) {
      if (r.target_id == tid) return &r;
    }
    return nullptr;
  };

  CHECK(find_target("app.py::helper") == nullptr);

  // Verify Call 2: compute() -> exact/plain
  const auto* r2 = find_target("app.py::compute");
  REQUIRE(r2 != nullptr);
  CHECK(r2->confidence == "exact");
  CHECK(r2->call_kind == "plain");

  // Verify Call 3: Worker() -> exact/constructor
  const auto* r3 = find_target("app.py::Worker.__init__");
  REQUIRE(r3 != nullptr);
  CHECK(r3->confidence == "exact");
  CHECK(r3->call_kind == "constructor");

  // Verify Call 4: calculate_total() -> exact/plain
  const auto* r4 = find_target("billing.py::calculate_total");
  REQUIRE(r4 != nullptr);
  CHECK(r4->confidence == "exact");
  CHECK(r4->call_kind == "plain");

  // Verify Call 5B: MathUtil.add() -> exact/plain (static method)
  const auto* r5 = find_target("app.py::MathUtil.add");
  REQUIRE(r5 != nullptr);
  CHECK(r5->confidence == "exact");
  CHECK(r5->call_kind == "plain");

  // Verify Call 6: self.do_work() -> exact/instance
  const auto* r6 = find_target("app.py::Worker.do_work");
  REQUIRE(r6 != nullptr);
  CHECK(r6->confidence == "exact");
  CHECK(r6->call_kind == "instance");

  // Verify Call 7: super().__init__() -> exact/instance
  const auto* r7 = find_target("app.py::Base.__init__");
  REQUIRE(r7 != nullptr);
  CHECK(r7->confidence == "exact");
  CHECK(r7->call_kind == "instance");

  // Verify Call 8: send_payload() -> heuristic/instance
  const auto* r8 = find_target("network.py::send_payload");
  REQUIRE(r8 != nullptr);
  CHECK(r8->confidence == "heuristic");
  CHECK(r8->call_kind == "instance");
}

TEST_CASE("Reverse Dependency Tracking") {
  FactStore store;
  store.put(make_empty_facts("models.py"));
  store.put(make_empty_facts("billing.py"));
  store.put(make_empty_facts("checkout.py"));
  store.put(make_empty_facts("reports.py"));

  Resolver resolver;

  // billing.py imports models.py
  ImportFact i_bill;
  i_bill.spec = "models";
  resolver.update_dependencies("billing.py", {i_bill}, store);

  // checkout.py imports billing.py and models.py
  ImportFact i_check1;
  i_check1.spec = "billing";
  ImportFact i_check2;
  i_check2.spec = "models";
  resolver.update_dependencies("checkout.py", {i_check1, i_check2}, store);

  // reports.py imports billing.py
  ImportFact i_rep;
  i_rep.spec = "billing";
  resolver.update_dependencies("reports.py", {i_rep}, store);

  // Verify dependents of billing.py: checkout.py, reports.py
  auto deps_bill = resolver.dependents_of("billing.py");
  REQUIRE(deps_bill.size() == 2);
  CHECK(deps_bill[0] == "checkout.py");
  CHECK(deps_bill[1] == "reports.py");

  // Verify dependents of models.py: billing.py, checkout.py
  auto deps_mod = resolver.dependents_of("models.py");
  REQUIRE(deps_mod.size() == 2);
  CHECK(deps_mod[0] == "billing.py");
  CHECK(deps_mod[1] == "checkout.py");

  // Remove reports.py
  resolver.remove_file("reports.py");
  deps_bill = resolver.dependents_of("billing.py");
  REQUIRE(deps_bill.size() == 1);
  CHECK(deps_bill[0] == "checkout.py");
}

TEST_CASE("Benchmark Project 4 Callers of calculate_total") {
  FactStore store;

  // 1. billing.py
  auto billing = make_empty_facts("billing.py", Language::Python);
  billing.symbols.push_back(make_func("billing.py", "calculate_total"));
  billing.symbols.push_back(make_class("billing.py", "Invoice"));
  billing.symbols.push_back(make_method("billing.py", "Invoice", "total"));
  CallFact c_inv;
  c_inv.caller_id = "billing.py::Invoice.total";
  c_inv.shape = CalleeShape::Name;
  c_inv.name = "calculate_total";
  billing.calls.push_back(c_inv);
  store.put(std::move(billing));

  // 2. checkout.py
  auto checkout = make_empty_facts("checkout.py", Language::Python);
  checkout.symbols.push_back(make_func("checkout.py", "checkout"));
  ImportFact i_co;
  i_co.kind = ImportFact::FromNames;
  i_co.spec = "billing";
  i_co.names = {{"calculate_total", "calculate_total"}};
  checkout.imports.push_back(i_co);
  CallFact c_co;
  c_co.caller_id = "checkout.py::checkout";
  c_co.shape = CalleeShape::Name;
  c_co.name = "calculate_total";
  checkout.calls.push_back(c_co);
  store.put(std::move(checkout));

  // 3. reports.py
  auto reports = make_empty_facts("reports.py", Language::Python);
  reports.symbols.push_back(make_func("reports.py", "daily_report"));
  ImportFact i_rep;
  i_rep.kind = ImportFact::FromNames;
  i_rep.spec = "billing";
  i_rep.names = {{"calculate_total", "calculate_total"}};
  reports.imports.push_back(i_rep);
  CallFact c_rep;
  c_rep.caller_id = "reports.py::daily_report";
  c_rep.shape = CalleeShape::Name;
  c_rep.name = "calculate_total";
  reports.calls.push_back(c_rep);
  store.put(std::move(reports));

  // 4. app.py
  auto app = make_empty_facts("app.py", Language::Python);
  app.symbols.push_back(make_func("app.py", "<module>"));
  ImportFact i_app;
  i_app.kind = ImportFact::FromNames;
  i_app.spec = "billing";
  i_app.names = {{"calculate_total", "calculate_total"}};
  app.imports.push_back(i_app);
  CallFact c_app;
  c_app.caller_id = "app.py::<module>";
  c_app.shape = CalleeShape::Name;
  c_app.name = "calculate_total";
  app.calls.push_back(c_app);
  store.put(std::move(app));

  Resolver resolver;
  resolver.rebuild_indices(store);

  // Collect callers across all files
  std::vector<std::string> callers;
  for (const auto& f : store.all_files()) {
    auto calls = resolver.resolve_file(store, f);
    for (const auto& call : calls) {
      if (call.target_id == "billing.py::calculate_total") {
        callers.push_back(call.caller_id);
      }
    }
  }

  std::sort(callers.begin(), callers.end());
  REQUIRE(callers.size() == 4);
  CHECK(callers[0] == "app.py::<module>");
  CHECK(callers[1] == "billing.py::Invoice.total");
  CHECK(callers[2] == "checkout.py::checkout");
  CHECK(callers[3] == "reports.py::daily_report");
}
