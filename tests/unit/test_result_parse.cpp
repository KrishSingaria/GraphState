#include <doctest/doctest.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "core/error.hpp"
#include "db/result.hpp"

using namespace gs;
using namespace gs::db;

namespace {

redisReply* make_string_reply(int type, const std::string& str) {
  redisReply* r = static_cast<redisReply*>(std::calloc(1, sizeof(redisReply)));
  r->type = type;
  r->len = str.size();
  r->str = static_cast<char*>(std::malloc(str.size() + 1));
  std::memcpy(r->str, str.data(), str.size());
  r->str[str.size()] = '\0';
  return r;
}

redisReply* make_array_reply(const std::vector<redisReply*>& elements) {
  redisReply* r = static_cast<redisReply*>(std::calloc(1, sizeof(redisReply)));
  r->type = REDIS_REPLY_ARRAY;
  r->elements = elements.size();
  r->element = static_cast<redisReply**>(std::calloc(elements.size(), sizeof(redisReply*)));
  for (size_t i = 0; i < elements.size(); ++i) {
    r->element[i] = elements[i];
  }
  return r;
}

void free_reply(redisReply* r) {
  if (!r) return;
  if (r->type == REDIS_REPLY_ARRAY && r->element) {
    for (size_t i = 0; i < r->elements; ++i) {
      free_reply(r->element[i]);
    }
    std::free(r->element);
  }
  if (r->str) {
    std::free(r->str);
  }
  std::free(r);
}

} // namespace

TEST_CASE("result_parse: parse_rows with standard 3-element verbose reply") {
  // Header: ["j"]
  redisReply* header = make_array_reply({make_string_reply(REDIS_REPLY_STRING, "j")});

  // Rows: [ ["{\"id\":\"a.py\",\"score\":10}"], ["{\"id\":\"b.py\",\"score\":20}"] ]
  redisReply* row1 = make_array_reply({make_string_reply(REDIS_REPLY_STRING, "{\"id\":\"a.py\",\"score\":10}")});
  redisReply* row2 = make_array_reply({make_string_reply(REDIS_REPLY_STRING, "{\"id\":\"b.py\",\"score\":20}")});
  redisReply* rows = make_array_reply({row1, row2});

  // Stats
  redisReply* stats = make_array_reply({
      make_string_reply(REDIS_REPLY_STRING, "Cached execution: 1"),
      make_string_reply(REDIS_REPLY_STRING, "Query internal execution time: 0.15 milliseconds")
  });

  redisReply* top = make_array_reply({header, rows, stats});

  auto parsed = parse_rows(top, "test_query");
  REQUIRE(parsed.size() == 2);
  CHECK(parsed[0]["id"] == "a.py");
  CHECK(parsed[0]["score"] == 10);
  CHECK(parsed[1]["id"] == "b.py");
  CHECK(parsed[1]["score"] == 20);

  QueryStats qs = parse_stats(top, "test_query");
  CHECK(qs.cached == true);
  CHECK(qs.exec_ms == doctest::Approx(0.15));

  free_reply(top);
}

TEST_CASE("result_parse: parse_stats for write query") {
  redisReply* stats = make_array_reply({
      make_string_reply(REDIS_REPLY_STRING, "Nodes created: 4"),
      make_string_reply(REDIS_REPLY_STRING, "Nodes deleted: 1"),
      make_string_reply(REDIS_REPLY_STRING, "Relationships created: 6"),
      make_string_reply(REDIS_REPLY_STRING, "Relationships deleted: 2"),
      make_string_reply(REDIS_REPLY_STRING, "Properties set: 12"),
      make_string_reply(REDIS_REPLY_STRING, "Cached execution: 0"),
      make_string_reply(REDIS_REPLY_STRING, "Query internal execution time: 1.45 milliseconds")
  });

  redisReply* top = make_array_reply({stats});

  auto rows = parse_rows(top, "write_query");
  CHECK(rows.empty());

  QueryStats qs = parse_stats(top, "write_query");
  CHECK(qs.nodes_created == 4);
  CHECK(qs.nodes_deleted == 1);
  CHECK(qs.rels_created == 6);
  CHECK(qs.rels_deleted == 2);
  CHECK(qs.props_set == 12);
  CHECK(qs.cached == false);
  CHECK(qs.exec_ms == doctest::Approx(1.45));

  free_reply(top);
}

TEST_CASE("result_parse: error handling") {
  CHECK_THROWS_AS(parse_rows(nullptr, "null_test"), gs::Error);
  CHECK_THROWS_AS(parse_stats(nullptr, "null_test"), gs::Error);

  redisReply* err = make_string_reply(REDIS_REPLY_ERROR, "Syntax error at offset 5");
  CHECK_THROWS_AS(parse_rows(err, "err_test"), gs::Error);
  CHECK_THROWS_AS(parse_stats(err, "err_test"), gs::Error);
  free_reply(err);

  // Corrupt JSON in column j
  redisReply* header = make_array_reply({make_string_reply(REDIS_REPLY_STRING, "j")});
  redisReply* bad_row = make_array_reply({make_string_reply(REDIS_REPLY_STRING, "{invalid_json:")});
  redisReply* rows = make_array_reply({bad_row});
  redisReply* stats = make_array_reply({make_string_reply(REDIS_REPLY_STRING, "Query internal execution time: 0.1 ms")});
  redisReply* top_bad = make_array_reply({header, rows, stats});

  CHECK_THROWS_AS(parse_rows(top_bad, "bad_json"), gs::Error);
  free_reply(top_bad);
}
