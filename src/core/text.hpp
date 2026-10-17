#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::core {

// ── Edit distance & normalization ──────────────────────────────────────────

// Computes the Damerau-Levenshtein distance between strings a and b (insert, delete, substitute, transpose)
size_t damerau_levenshtein(std::string_view a, std::string_view b);

// Splits identifier (e.g. CamelCase, snake_case, HTTPServer) into lower-case words
std::vector<std::string> split_identifier(std::string_view s);

// Normalizes intent goal: lowercase, trimmed, whitespace runs collapsed to single space,
// trailing punctuation stripped (. ! ? ;)
std::string goal_norm(std::string_view s);

// ── Indentation and text manipulation ──────────────────────────────────────

// Removes common leading whitespace across all non-empty lines
std::string dedent(std::string_view text);

// Reindents a multi-line block to a target indentation prefix (e.g. 4 spaces or tabs)
std::string reindent(std::string_view text, std::string_view indent_prefix);

// Converts line endings
std::string to_crlf(std::string_view text);
std::string to_lf(std::string_view text);

// ── Line index (1-based line/col <-> byte offset) ──────────────────────────

struct Position {
  uint32_t line = 1; // 1-based
  uint32_t col = 1;  // 1-based byte column
};

class LineIndex {
public:
  explicit LineIndex(std::string_view text);

  // Maps byte offset to 1-based line and 1-based column
  Position position_at(size_t byte_offset) const;

  // Maps 1-based line and 1-based column to byte offset
  size_t offset_at(uint32_t line, uint32_t col) const;

  size_t line_count() const { return line_starts_.size(); }

private:
  std::vector<size_t> line_starts_;
  size_t total_bytes_ = 0;
};

} // namespace gs::core
