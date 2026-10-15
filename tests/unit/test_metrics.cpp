#include <doctest/doctest.h>
#include <string>
#include "lang/language.hpp"
#include "lang/metrics.hpp"
#include "lang/parser.hpp"

using namespace gs::lang;

TEST_CASE("Metrics calculates cyclomatic complexity and nesting depth") {
  SUBCASE("Python linear function") {
    std::string code = "def f(x):\n    a = x + 1\n    return a\n";
    TreePtr tree = parse_string(Language::Python, code);
    REQUIRE(tree != nullptr);
    TSNode root = ts_tree_root_node(tree.get());
    TSNode fn = ts_node_named_child(root, 0);
    TSNode body = ts_node_child_by_field_name(fn, "body", 4);
    REQUIRE(!ts_node_is_null(body));

    auto m = compute_metrics(body, Language::Python, code);
    CHECK(m.complexity == 1);
    CHECK(m.max_nesting == 0);
  }

  SUBCASE("Python branches and loops") {
    std::string code = R"(
def f(x, y):
    if x > 0:
        for i in range(x):
            if i % 2 == 0:
                y += 1
            else:
                y -= 1
    elif x < 0:
        while y > 0:
            y -= 1
    return y
)";
    TreePtr tree = parse_string(Language::Python, code);
    REQUIRE(tree != nullptr);
    TSNode root = ts_tree_root_node(tree.get());
    TSNode fn = ts_node_named_child(root, 0);
    TSNode body = ts_node_child_by_field_name(fn, "body", 4);
    REQUIRE(!ts_node_is_null(body));

    auto m = compute_metrics(body, Language::Python, code);
    // Decision points: if x > 0, for i, if i%2==0, elif x < 0, while y > 0 -> 5 decision nodes => complexity 6
    CHECK(m.complexity >= 5);
    // Nesting: if (1) -> for (2) -> if (3) => max_nesting >= 3
    CHECK(m.max_nesting >= 3);
  }

  SUBCASE("TypeScript decision nodes") {
    std::string code = R"(
function process(a: number, b: boolean): number {
  if (b && a > 10) {
    return a > 20 ? a : 20;
  }
  return 0;
}
)";
    TreePtr tree = parse_string(Language::TypeScript, code);
    REQUIRE(tree != nullptr);
    TSNode root = ts_tree_root_node(tree.get());
    TSNode fn = ts_node_named_child(root, 0);
    TSNode body = ts_node_child_by_field_name(fn, "body", 4);
    REQUIRE(!ts_node_is_null(body));

    auto m = compute_metrics(body, Language::TypeScript, code);
    // Decision points: if, &&, ternary ?: complexity >= 3
    CHECK(m.complexity >= 3);
    CHECK(m.max_nesting >= 1);
  }
}
