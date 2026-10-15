#include <doctest/doctest.h>
#include <string>
#include "lang/language.hpp"
#include "lang/parser.hpp"
#include "lang/syntax.hpp"

using namespace gs::lang;

TEST_CASE("Syntax Collector captures errors in invalid syntax") {
  SUBCASE("Valid Python has no syntax errors") {
    std::string code = "def foo(x):\n    return x + 1\n";
    TreePtr tree = parse_string(Language::Python, code);
    REQUIRE(tree != nullptr);
    auto errors = collect_syntax_errors(ts_tree_root_node(tree.get()), code);
    CHECK(errors.empty());
  }

  SUBCASE("Python with syntax error is captured") {
    std::string code = "def foo(x\n    return x + 1\n"; // missing closing paren and colon
    TreePtr tree = parse_string(Language::Python, code);
    REQUIRE(tree != nullptr);
    auto errors = collect_syntax_errors(ts_tree_root_node(tree.get()), code);
    CHECK(!errors.empty());
    CHECK(errors[0].line >= 1);
  }

  SUBCASE("TypeScript with syntax error is captured") {
    std::string code = "function test( { return 42; }"; // malformed params
    TreePtr tree = parse_string(Language::TypeScript, code);
    REQUIRE(tree != nullptr);
    auto errors = collect_syntax_errors(ts_tree_root_node(tree.get()), code);
    CHECK(!errors.empty());
    CHECK(errors[0].line >= 1);
  }
}
