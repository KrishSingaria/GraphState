#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "core/text.hpp"

TEST_CASE("unit: text damerau_levenshtein") {
  using gs::core::damerau_levenshtein;

  CHECK(damerau_levenshtein("", "") == 0);
  CHECK(damerau_levenshtein("a", "") == 1);
  CHECK(damerau_levenshtein("", "b") == 1);
  CHECK(damerau_levenshtein("kitten", "sitting") == 3);
  CHECK(damerau_levenshtein("billing", "billing") == 0);

  // Transposition
  CHECK(damerau_levenshtein("ab", "ba") == 1);
  CHECK(damerau_levenshtein("calculate", "calcualte") == 1);
  CHECK(damerau_levenshtein("calculate_total", "calc_total") <= 5);
}

TEST_CASE("unit: text split_identifier") {
  using gs::core::split_identifier;

  CHECK(split_identifier("calculateTotal") == std::vector<std::string>{"calculate", "total"});
  CHECK(split_identifier("calc_total") == std::vector<std::string>{"calc", "total"});
  CHECK(split_identifier("HTTPServer") == std::vector<std::string>{"http", "server"});
  CHECK(split_identifier("getHTTPResponseCode") == std::vector<std::string>{"get", "http", "response", "code"});
  CHECK(split_identifier("Invoice123Item") == std::vector<std::string>{"invoice123", "item"});
}

TEST_CASE("unit: text goal_norm") {
  using gs::core::goal_norm;

  CHECK(goal_norm("  Add currency to totals. ") == "add currency to totals");
  CHECK(goal_norm("Refactor   Billing  Logic!!!") == "refactor billing logic");
  CHECK(goal_norm("Fix bug in checkout?") == "fix bug in checkout");
  CHECK(goal_norm("Do nothing;") == "do nothing");
}

TEST_CASE("unit: text dedent and reindent") {
  using gs::core::dedent;
  using gs::core::reindent;

  std::string code = "    def foo():\n        x = 1\n        return x\n";
  std::string expected = "def foo():\n    x = 1\n    return x\n";
  CHECK(dedent(code) == expected);

  std::string reindented = reindent(expected, "  ");
  std::string expected_re = "  def foo():\n      x = 1\n      return x\n";
  CHECK(reindented == expected_re);
}

TEST_CASE("unit: text LineIndex") {
  using gs::core::LineIndex;

  std::string src = "line1\nline2\nline3";
  LineIndex idx(src);
  CHECK(idx.line_count() == 3);

  auto p1 = idx.position_at(0);
  CHECK(p1.line == 1);
  CHECK(p1.col == 1);

  auto p2 = idx.position_at(6); // 'l' in "line2"
  CHECK(p2.line == 2);
  CHECK(p2.col == 1);

  auto p3 = idx.position_at(9); // 'e' in "line2"
  CHECK(p3.line == 2);
  CHECK(p3.col == 4);

  CHECK(idx.offset_at(2, 1) == 6);
  CHECK(idx.offset_at(2, 4) == 9);
}
