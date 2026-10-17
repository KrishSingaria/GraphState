#pragma once

#define XXH_INLINE_ALL
#include <xxhash.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace gs::core {

inline std::string xxh3_hex(std::string_view data) {
  uint64_t h = XXH3_64bits(data.data(), data.size());
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return std::string(buf);
}

inline std::string hash_bytes(std::string_view data) {
  return xxh3_hex(data);
}

inline std::string collapse_whitespace(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  bool in_space = false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!in_space && !out.empty()) {
        out += ' ';
        in_space = true;
      }
    } else {
      out += c;
      in_space = false;
    }
  }
  if (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

inline std::string body_hash(std::string_view body_src) {
  return xxh3_hex(collapse_whitespace(body_src));
}

} // namespace gs::core
