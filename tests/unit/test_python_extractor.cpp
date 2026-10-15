#include <doctest/doctest.h>
#include <fstream>
#include <string>
#include "lang/extractor.hpp"
#include "lang/language.hpp"
#include "lang/parser.hpp"

using namespace gs::lang;

TEST_CASE("Python Extractor extracts symbols, calls, and imports") {
  std::string code = R"(
"""Module docstring"""
import os
from math import sqrt as square_root

class Base:
    def __init__(self, name: str):
        self.name = name

class Service(Base):
    def __init__(self, name: str, retries: int = 3):
        super().__init__(name)
        self.retries = retries

    def execute(self, task: str) -> bool:
        """Execute a task."""
        self.log(task)
        return True

    def log(self, msg: str):
        pass

    @staticmethod
    def util():
        return 42

def compute(x: int, y: int = 10, *, flag: bool = True) -> int:
    val = square_root(x)
    return int(val) + y

def main():
    srv = Service("worker")
    srv.execute("job1")
    compute(16, flag=False)
)";

  TreePtr tree = parse_string(Language::Python, code);
  REQUIRE(tree != nullptr);

  auto extractor = make_extractor(Language::Python);
  REQUIRE(extractor != nullptr);

  FileFacts facts = extractor->extract("app.py", code, tree.get());

  CHECK(facts.file == "app.py");
  CHECK(facts.lang == Language::Python);
  CHECK(facts.parse_ok);
  CHECK(facts.syntax_errors.empty());

  // Find symbols
  const SymbolFact* mod = facts.find("<module>");
  REQUIRE(mod != nullptr);
  CHECK(mod->kind == SymbolKind::Module);

  const SymbolFact* base_cls = facts.find("Base");
  REQUIRE(base_cls != nullptr);
  CHECK(base_cls->kind == SymbolKind::Class);

  const SymbolFact* srv_cls = facts.find("Service");
  REQUIRE(srv_cls != nullptr);
  CHECK(srv_cls->kind == SymbolKind::Class);
  REQUIRE(srv_cls->bases.size() == 1);
  CHECK(srv_cls->bases[0] == "Base");

  const SymbolFact* srv_init = facts.find("Service.__init__");
  REQUIRE(srv_init != nullptr);
  CHECK(srv_init->kind == SymbolKind::Constructor);
  CHECK(srv_init->sig.drop_first_for_instance_call == true);
  REQUIRE(srv_init->sig.params.size() == 3);
  CHECK(srv_init->sig.params[0].name == "self");
  CHECK(srv_init->sig.params[1].name == "name");
  CHECK(srv_init->sig.params[2].name == "retries");
  CHECK(srv_init->sig.params[2].has_default == true);

  const SymbolFact* srv_exec = facts.find("Service.execute");
  REQUIRE(srv_exec != nullptr);
  CHECK(srv_exec->kind == SymbolKind::Method);
  CHECK(srv_exec->doc == "Execute a task.");
  CHECK(srv_exec->sig.return_type == "bool");

  const SymbolFact* srv_util = facts.find("Service.util");
  REQUIRE(srv_util != nullptr);
  CHECK(srv_util->is_static == true);
  CHECK(srv_util->sig.drop_first_for_instance_call == false);

  const SymbolFact* fn_compute = facts.find("compute");
  REQUIRE(fn_compute != nullptr);
  CHECK(fn_compute->kind == SymbolKind::Function);
  REQUIRE(fn_compute->sig.params.size() == 3);
  CHECK(fn_compute->sig.params[0].kind == ParamKind::PositionalOrKeyword);
  CHECK(fn_compute->sig.params[1].kind == ParamKind::PositionalOrKeyword);
  CHECK(fn_compute->sig.params[1].has_default == true);
  CHECK(fn_compute->sig.params[2].kind == ParamKind::KeywordOnly);
  CHECK(fn_compute->sig.params[2].has_default == true);

  // Imports
  CHECK(facts.imports.size() >= 2);
  bool found_os = false;
  bool found_math = false;
  for (const auto& imp : facts.imports) {
    if (imp.spec == "os") found_os = true;
    if (imp.spec == "math") {
      found_math = true;
      REQUIRE(!imp.names.empty());
      CHECK(imp.names[0].first == "sqrt");
      CHECK(imp.names[0].second == "square_root");
    }
  }
  CHECK(found_os);
  CHECK(found_math);

  // Calls
  CHECK(!facts.calls.empty());
  bool found_super_call = false;
  bool found_self_log = false;
  for (const auto& c : facts.calls) {
    if (c.shape == CalleeShape::SuperAttr && c.name == "__init__") {
      found_super_call = true;
    }
    if (c.shape == CalleeShape::SelfAttr && c.name == "log") {
      found_self_log = true;
    }
  }
  CHECK(found_super_call);
  CHECK(found_self_log);
}
