#include <doctest/doctest.h>
#include <limits>
#include "core/error.hpp"
#include "db/cypher_params.hpp"

using namespace gs;
using namespace gs::db;

TEST_CASE("cypher_params: literal encoding primitives") {
  CHECK(encode_literal(nullptr) == "null");
  CHECK(encode_literal(true) == "true");
  CHECK(encode_literal(false) == "false");
  CHECK(encode_literal(0) == "0");
  CHECK(encode_literal(-42) == "-42");
  CHECK(encode_literal(123456789012345LL) == "123456789012345");
  CHECK(encode_literal(3.5) == "3.5");
  CHECK(encode_literal(2.0) == "2.0");
  CHECK(std::stod(encode_literal(3.14)) == doctest::Approx(3.14));
}

TEST_CASE("cypher_params: string escaping") {
  CHECK(encode_literal("hello") == "'hello'");
  CHECK(encode_literal("hello 'world'") == "'hello \\'world\\''");
  CHECK(encode_literal("path\\to\\file") == "'path\\\\to\\\\file'");
  CHECK(encode_literal("line1\nline2") == "'line1\\nline2'");
  CHECK(encode_literal("a\rb\tc") == "'a\\rb\\tc'");
  CHECK(encode_literal("with \"quotes\" and $params and {brackets}") == "'with \"quotes\" and $params and {brackets}'");
  CHECK(encode_literal("emoji: 🚀 graph") == "'emoji: 🚀 graph'");
}

TEST_CASE("cypher_params: array and object encoding") {
  nlohmann::json arr = nlohmann::json::array({1, "two", 3.5, false});
  CHECK(encode_literal(arr) == "[1, 'two', 3.5, false]");

  nlohmann::json empty_arr = nlohmann::json::array();
  CHECK(encode_literal(empty_arr) == "[]");

  nlohmann::json obj = {{"id", "src/billing.py"}, {"count", 5}};
  // JSON keys in nlohmann::json are sorted alphabetically
  CHECK(encode_literal(obj) == "{count: 5, id: 'src/billing.py'}");

  nlohmann::json empty_obj = nlohmann::json::object();
  CHECK(encode_literal(empty_obj) == "{}");

  nlohmann::json nested = {
      {"rows", nlohmann::json::array({
          nlohmann::json{{"a", 1}},
          nlohmann::json{{"a", 2}}
      })}
  };
  CHECK(encode_literal(nested) == "{rows: [{a: 1}, {a: 2}]}");
}

TEST_CASE("cypher_params: encode_params prefix") {
  CHECK(encode_params(nullptr) == "");
  CHECK(encode_params(nlohmann::json::object()) == "");

  nlohmann::json params = {
      {"id", "a.py::f"},
      {"n", 3},
      {"rows", nlohmann::json::array({nlohmann::json{{"a", 1}}})}
  };
  std::string encoded = encode_params(params);
  CHECK(encoded.rfind("CYPHER ", 0) == 0);
  CHECK(encoded.find("id='a.py::f'") != std::string::npos);
  CHECK(encoded.find("n=3") != std::string::npos);
  CHECK(encoded.find("rows=[{a: 1}]") != std::string::npos);
}

TEST_CASE("cypher_params: validation and error handling") {
  // Non-object params
  CHECK_THROWS_AS(encode_params(nlohmann::json::array({1, 2})), gs::Error);
  CHECK_THROWS_AS(encode_params("string"), gs::Error);

  // Invalid param names
  CHECK_THROWS_AS(encode_params(nlohmann::json{{"1invalid", "val"}}), gs::Error);
  CHECK_THROWS_AS(encode_params(nlohmann::json{{"invalid-name", "val"}}), gs::Error);
  CHECK_THROWS_AS(encode_params(nlohmann::json{{"invalid name", "val"}}), gs::Error);

  // Invalid object keys in literal
  nlohmann::json bad_obj = {{"bad-key", 123}};
  CHECK_THROWS_AS(encode_literal(bad_obj), gs::Error);

  // NaN / Inf
  double nan_val = std::numeric_limits<double>::quiet_NaN();
  CHECK_THROWS_AS(encode_literal(nan_val), gs::Error);

  double inf_val = std::numeric_limits<double>::infinity();
  CHECK_THROWS_AS(encode_literal(inf_val), gs::Error);

  // Nesting depth limit
  nlohmann::json deeply_nested = 1;
  for (int i = 0; i < 105; ++i) {
    deeply_nested = nlohmann::json::array({deeply_nested});
  }
  CHECK_THROWS_AS(encode_literal(deeply_nested), gs::Error);
}

TEST_CASE("cypher_params: fulltext_query sanitizer") {
  // Simple words
  std::string q1 = fulltext_query("calculate total");
  CHECK(q1 == "calculate* | total*");

  // CamelCase and snake_case splitting
  std::string q2 = fulltext_query("calculateTotal calc_sum");
  CHECK(q2.find("calculate*") != std::string::npos);
  CHECK(q2.find("total*") != std::string::npos);
  CHECK(q2.find("calc*") != std::string::npos);
  CHECK(q2.find("sum*") != std::string::npos);

  // Stopwords and short tokens (< 2) dropped
  std::string q3 = fulltext_query("the a is of function in to for");
  CHECK(q3 == "function*");

  // Short tokens (len == 2) keep as is (no '*')
  std::string q4 = fulltext_query("db io");
  CHECK(q4 == "db | io");

  // RediSearch special chars stripped
  std::string q5 = fulltext_query("@title -tag | (foo) {bar} [baz] * % : ~");
  CHECK(q5.find("@") == std::string::npos);
  CHECK(q5.find("(") == std::string::npos);
  CHECK(q5.find("[") == std::string::npos);
  CHECK(q5.find("title*") != std::string::npos);
  CHECK(q5.find("tag*") != std::string::npos);
  CHECK(q5.find("foo*") != std::string::npos);

  // Empty string when no valid tokens
  CHECK(fulltext_query("   ") == "");
  CHECK(fulltext_query("the an is") == "");
  CHECK(fulltext_query("!@#$%^&*") == "");
}
