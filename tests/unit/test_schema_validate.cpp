#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "mcp/schema_validate.hpp"

TEST_CASE("unit: schema_validate basic types and required") {
  using namespace gs::mcp;

  nlohmann::json schema = {
      {"type", "object"},
      {"additionalProperties", false},
      {"required", {"query"}},
      {"properties", {
          {"query", {{"type", "string"}, {"minLength", 1}, {"maxLength", 100}}},
          {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"default", 10}}},
          {"kind", {{"type", "string"}, {"enum", {"any", "function", "class"}}, {"default", "any"}}}
      }}
  };

  SUBCASE("valid instance with default injection") {
    nlohmann::json inst = {{"query", "calculate_total"}};
    auto errs = validate_schema(schema, inst);
    CHECK(errs.empty());
    CHECK(inst["limit"] == 10);
    CHECK(inst["kind"] == "any");
  }

  SUBCASE("missing required field") {
    nlohmann::json inst = {{"limit", 5}};
    auto errs = validate_schema(schema, inst);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].path == "/query");
  }

  SUBCASE("additional property disallowed") {
    nlohmann::json inst = {{"query", "foo"}, {"extra", 123}};
    auto errs = validate_schema(schema, inst);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].path == "/extra");
  }

  SUBCASE("enum violation") {
    nlohmann::json inst = {{"query", "foo"}, {"kind", "module"}};
    auto errs = validate_schema(schema, inst);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].path == "/kind");
  }

  SUBCASE("number range violation") {
    nlohmann::json inst = {{"query", "foo"}, {"limit", 100}};
    auto errs = validate_schema(schema, inst);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].path == "/limit");
  }

  SUBCASE("string length violation") {
    nlohmann::json inst = {{"query", ""}};
    auto errs = validate_schema(schema, inst);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].path == "/query");
  }
}

TEST_CASE("unit: schema_validate nested arrays and items") {
  using namespace gs::mcp;

  nlohmann::json schema = {
      {"type", "object"},
      {"required", {"edits"}},
      {"properties", {
          {"edits", {
              {"type", "array"},
              {"minItems", 1},
              {"items", {
                  {"type", "object"},
                  {"required", {"op", "file"}},
                  {"properties", {
                      {"op", {{"type", "string"}, {"enum", {"replace_symbol", "insert_symbol"}}}},
                      {"file", {{"type", "string"}}}
                  }}
              }}
          }}
      }}
  };

  SUBCASE("valid nested array") {
    nlohmann::json inst = {
        {"edits", {
            {{"op", "replace_symbol"}, {"file", "billing.py"}}
        }}
    };
    auto errs = validate_schema(schema, inst);
    CHECK(errs.empty());
  }

  SUBCASE("nested enum violation") {
    nlohmann::json inst = {
        {"edits", {
            {{"op", "invalid_op"}, {"file", "billing.py"}}
        }}
    };
    auto errs = validate_schema(schema, inst);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].path == "/edits/0/op");
  }
}
