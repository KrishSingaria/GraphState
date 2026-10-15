#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace gs::db {

// Encodes JSON params into FalkorDB parameter prefix:
// {"id":"a.py::f","n":3,"rows":[{"a":1}]}  ->  "CYPHER id='a.py::f' n=3 rows=[{a:1}]"
// Returns empty string if params is null or an empty object.
// Throws gs::Error(ErrorCode::INVALID_PARAM) on invalid key names, types, NaN/Inf, or bad structure.
std::string encode_params(const nlohmann::json& params);

// Encodes a single JSON value into a Cypher literal.
// Throws gs::Error(ErrorCode::INVALID_PARAM) on depth > 100, invalid object keys, or NaN/Inf.
std::string encode_literal(const nlohmann::json& v, int depth = 0);

// Sanitizes user search text into a safe RediSearch query (§4.9).
// Splits tokens on non-alphanumerics, handles camelCase and snake_case,
// lowercases, filters stopwords and <2 char tokens, appends '*' for length >= 3,
// deduplicates, limits to 8 tokens, and joins with " | ".
std::string fulltext_query(std::string_view user_text);

} // namespace gs::db
