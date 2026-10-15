#include "db/cypher_params.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include "core/error.hpp"

namespace gs::db {

namespace {

bool is_valid_ident(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  char first = name[0];
  if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) {
    return false;
  }
  for (size_t i = 1; i < name.size(); ++i) {
    char c = name[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

std::string escape_string(std::string_view s) {
  std::string out = "'";
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\'':
        out += "\\'";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  out += '\'';
  return out;
}

const std::unordered_set<std::string_view> kStopwords = {
    "the", "a", "an", "and", "or", "of", "to", "in", "for", "on", "with", "is", "are", "be", "this", "that", "it"
};

std::string to_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// Splits a single raw token on camelCase and snake_case while keeping the original token as well.
// E.g. "calculateTotal" -> ["calculatetotal", "calculate", "total"]
//      "calc_total" -> ["calc_total", "calc", "total"]
void split_token_subwords(std::string_view raw, std::vector<std::string>& out) {
  if (raw.empty()) {
    return;
  }
  out.push_back(to_lower(raw));

  // If contains underscore, split on '_'
  if (raw.find('_') != std::string_view::npos) {
    size_t start = 0;
    while (start < raw.size()) {
      while (start < raw.size() && raw[start] == '_') {
        ++start;
      }
      if (start >= raw.size()) {
        break;
      }
      size_t end = start;
      while (end < raw.size() && raw[end] != '_') {
        ++end;
      }
      out.push_back(to_lower(raw.substr(start, end - start)));
      start = end;
    }
    return;
  }

  // Check for camelCase (e.g. calculateTotal or URLParser)
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 1; i < raw.size(); ++i) {
    bool lower_to_upper = std::islower(static_cast<unsigned char>(raw[i - 1])) &&
                          std::isupper(static_cast<unsigned char>(raw[i]));
    bool upper_series = std::isupper(static_cast<unsigned char>(raw[i - 1])) &&
                        std::isupper(static_cast<unsigned char>(raw[i])) &&
                        (i + 1 < raw.size() && std::islower(static_cast<unsigned char>(raw[i + 1])));
    if (lower_to_upper || upper_series) {
      size_t split_pos = lower_to_upper ? i : i;
      if (split_pos > start) {
        parts.push_back(to_lower(raw.substr(start, split_pos - start)));
        start = split_pos;
      }
    }
  }
  if (start < raw.size() && !parts.empty()) {
    parts.push_back(to_lower(raw.substr(start)));
  }
  for (auto& p : parts) {
    out.push_back(std::move(p));
  }
}

} // namespace

std::string encode_literal(const nlohmann::json& v, int depth) {
  if (depth > 100) {
    throw gs::Error(ErrorCode::INVALID_PARAM, "parameter nesting depth exceeds 100");
  }

  if (v.is_null()) {
    return "null";
  }
  if (v.is_boolean()) {
    return v.get<bool>() ? "true" : "false";
  }
  if (v.is_number_integer()) {
    return std::to_string(v.get<int64_t>());
  }
  if (v.is_number_unsigned()) {
    return std::to_string(v.get<uint64_t>());
  }
  if (v.is_number_float()) {
    double d = v.get<double>();
    if (std::isnan(d) || std::isinf(d)) {
      throw gs::Error(ErrorCode::INVALID_PARAM, "NaN or Inf float not supported in Cypher literal");
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    std::string s(buf);
    // If output is plain integer without decimal point or exponent, add .0 to keep type float
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
      s += ".0";
    }
    return s;
  }
  if (v.is_string()) {
    return escape_string(v.get<std::string>());
  }
  if (v.is_array()) {
    std::string out = "[";
    bool first = true;
    for (const auto& elem : v) {
      if (!first) {
        out += ", ";
      }
      first = false;
      out += encode_literal(elem, depth + 1);
    }
    out += "]";
    return out;
  }
  if (v.is_object()) {
    std::string out = "{";
    bool first = true;
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (!is_valid_ident(it.key())) {
        throw gs::Error(ErrorCode::INVALID_PARAM, "invalid object key in Cypher literal: '" + it.key() + "'");
      }
      if (!first) {
        out += ", ";
      }
      first = false;
      out += it.key();
      out += ": ";
      out += encode_literal(it.value(), depth + 1);
    }
    out += "}";
    return out;
  }

  throw gs::Error(ErrorCode::INVALID_PARAM, "unsupported json type in Cypher literal");
}

std::string encode_params(const nlohmann::json& params) {
  if (params.is_null() || (params.is_object() && params.empty())) {
    return "";
  }
  if (!params.is_object()) {
    throw gs::Error(ErrorCode::INVALID_PARAM, "Cypher params must be a JSON object");
  }

  std::string out = "CYPHER";
  for (auto it = params.begin(); it != params.end(); ++it) {
    if (!is_valid_ident(it.key())) {
      throw gs::Error(ErrorCode::INVALID_PARAM, "invalid Cypher param name: '" + it.key() + "'");
    }
    out += " ";
    out += it.key();
    out += "=";
    out += encode_literal(it.value(), 0);
  }
  return out;
}

std::string fulltext_query(std::string_view user_text) {
  // 1. Split user text into base tokens on any char not in [A-Za-z0-9_]
  std::vector<std::string> subwords;
  size_t i = 0;
  while (i < user_text.size()) {
    while (i < user_text.size() &&
           !((user_text[i] >= 'A' && user_text[i] <= 'Z') ||
             (user_text[i] >= 'a' && user_text[i] <= 'z') ||
             (user_text[i] >= '0' && user_text[i] <= '9') ||
             user_text[i] == '_')) {
      ++i;
    }
    if (i >= user_text.size()) {
      break;
    }
    size_t start = i;
    while (i < user_text.size() &&
           ((user_text[i] >= 'A' && user_text[i] <= 'Z') ||
            (user_text[i] >= 'a' && user_text[i] <= 'z') ||
            (user_text[i] >= '0' && user_text[i] <= '9') ||
            user_text[i] == '_')) {
      ++i;
    }
    split_token_subwords(user_text.substr(start, i - start), subwords);
  }

  // 2. Filter: drop tokens of length < 2 and stopwords
  // 3. For each token: append '*' if len >= 3, else keep as-is
  // 4. Deduplicate preserving order
  std::vector<std::string> final_tokens;
  std::unordered_set<std::string> seen;

  for (const auto& w : subwords) {
    if (w.size() < 2) {
      continue;
    }
    if (kStopwords.find(w) != kStopwords.end()) {
      continue;
    }
    std::string token = (w.size() >= 3) ? (w + "*") : w;
    if (seen.insert(token).second) {
      final_tokens.push_back(std::move(token));
      if (final_tokens.size() >= 8) {
        break;
      }
    }
  }

  if (final_tokens.empty()) {
    return "";
  }

  std::string result;
  for (size_t idx = 0; idx < final_tokens.size(); ++idx) {
    if (idx > 0) {
      result += " | ";
    }
    result += final_tokens[idx];
  }
  return result;
}

} // namespace gs::db
