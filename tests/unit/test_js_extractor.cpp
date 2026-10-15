#include <doctest/doctest.h>
#include <string>
#include "lang/extractor.hpp"
#include "lang/language.hpp"
#include "lang/parser.hpp"

using namespace gs::lang;

TEST_CASE("JavaScript Extractor extracts symbols, CommonJS requires, and exports") {
  std::string code = R"(
const path = require('path');
const { format } = require('./formatter');

class DataHandler {
  constructor(tag) {
    this.tag = tag;
  }

  process(data) {
    return format(data);
  }
}

function processAll(items) {
  const handler = new DataHandler("main");
  for (const item of items) {
    handler.process(item);
  }
}

const calculateScore = (base, bonus = 0) => {
  return base + bonus;
};

module.exports = {
  DataHandler,
  processAll,
  calculateScore
};
)";

  TreePtr tree = parse_string(Language::JavaScript, code);
  REQUIRE(tree != nullptr);

  auto extractor = make_extractor(Language::JavaScript);
  REQUIRE(extractor != nullptr);

  FileFacts facts = extractor->extract("app.js", code, tree.get());

  CHECK(facts.file == "app.js");
  CHECK(facts.lang == Language::JavaScript);
  CHECK(facts.parse_ok);
  CHECK(facts.syntax_errors.empty());

  // Class
  const SymbolFact* cls = facts.find("DataHandler");
  REQUIRE(cls != nullptr);
  CHECK(cls->kind == SymbolKind::Class);

  // Method
  const SymbolFact* proc_m = facts.find("DataHandler.process");
  REQUIRE(proc_m != nullptr);
  CHECK(proc_m->kind == SymbolKind::Method);
  REQUIRE(proc_m->sig.params.size() == 1);
  CHECK(proc_m->sig.params[0].name == "data");

  // Function
  const SymbolFact* fn = facts.find("processAll");
  REQUIRE(fn != nullptr);
  CHECK(fn->kind == SymbolKind::Function);

  // Arrow function
  const SymbolFact* arrow = facts.find("calculateScore");
  REQUIRE(arrow != nullptr);
  CHECK(arrow->kind == SymbolKind::Arrow);
  REQUIRE(arrow->sig.params.size() == 2);
  CHECK(arrow->sig.params[0].name == "base");
  CHECK(arrow->sig.params[1].name == "bonus");
  CHECK(arrow->sig.params[1].has_default == true);

  // CommonJS requires
  REQUIRE(facts.imports.size() >= 2);
  bool found_path = false;
  bool found_formatter = false;
  for (const auto& imp : facts.imports) {
    if (imp.spec == "path") {
      found_path = true;
      CHECK(imp.kind == ImportFact::TsNamespace);
    }
    if (imp.spec == "./formatter") {
      found_formatter = true;
      CHECK(imp.kind == ImportFact::TsNamed);
    }
  }
  CHECK(found_path);
  CHECK(found_formatter);

  // Exports from module.exports
  CHECK(!facts.exports.empty());
  bool found_exp_dh = false;
  bool found_exp_pa = false;
  for (const auto& exp : facts.exports) {
    if (exp == "DataHandler") found_exp_dh = true;
    if (exp == "processAll") found_exp_pa = true;
  }
  CHECK(found_exp_dh);
  CHECK(found_exp_pa);
}
