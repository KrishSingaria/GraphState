#include "core/text.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace gs::core {

size_t damerau_levenshtein(std::string_view a, std::string_view b) {
  const size_t len_a = a.size();
  const size_t len_b = b.size();
  if (len_a == 0) return len_b;
  if (len_b == 0) return len_a;

  std::vector<std::vector<size_t>> d(len_a + 1, std::vector<size_t>(len_b + 1, 0));

  for (size_t i = 0; i <= len_a; ++i) d[i][0] = i;
  for (size_t j = 0; j <= len_b; ++j) d[0][j] = j;

  for (size_t i = 1; i <= len_a; ++i) {
    for (size_t j = 1; j <= len_b; ++j) {
      size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      d[i][j] = std::min({
          d[i - 1][j] + 1,     // deletion
          d[i][j - 1] + 1,     // insertion
          d[i - 1][j - 1] + cost // substitution
      });
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
        d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + cost); // transposition
      }
    }
  }

  return d[len_a][len_b];
}

std::vector<std::string> split_identifier(std::string_view s) {
  std::vector<std::string> words;
  std::string current;

  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '_' || c == '-' || std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        std::string word = current;
        for (char& ch : word) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        words.push_back(std::move(word));
        current.clear();
      }
      continue;
    }

    if (std::isupper(static_cast<unsigned char>(c))) {
      // Check if this is the start of a new word
      bool is_prev_lower = (i > 0 && std::islower(static_cast<unsigned char>(s[i - 1])));
      bool is_next_lower = (i + 1 < s.size() && std::islower(static_cast<unsigned char>(s[i + 1])));

      if (is_prev_lower || (!current.empty() && is_next_lower)) {
        if (!current.empty()) {
          std::string word = current;
          for (char& ch : word) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          words.push_back(std::move(word));
          current.clear();
        }
      }
    }

    current.push_back(c);
  }

  if (!current.empty()) {
    std::string word = current;
    for (char& ch : word) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    words.push_back(std::move(word));
  }

  return words;
}

std::string goal_norm(std::string_view s) {
  std::string res;
  res.reserve(s.size());

  // Trim leading whitespace
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }

  // Trim trailing whitespace and punctuation
  size_t end = s.size();
  while (end > start) {
    char c = s[end - 1];
    if (std::isspace(static_cast<unsigned char>(c)) || c == '.' || c == '!' || c == '?' || c == ';' || c == ',') {
      --end;
    } else {
      break;
    }
  }

  bool in_ws = false;
  for (size_t i = start; i < end; ++i) {
    char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_ws) {
        res.push_back(' ');
        in_ws = true;
      }
    } else {
      res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      in_ws = false;
    }
  }

  return res;
}

std::string dedent(std::string_view text) {
  if (text.empty()) return "";

  std::vector<std::string_view> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }

  // Find minimum leading whitespace on non-empty lines
  size_t min_indent = std::string_view::npos;
  for (auto line : lines) {
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    if (i < line.size()) { // Non-empty line
      if (min_indent == std::string_view::npos || i < min_indent) {
        min_indent = i;
      }
    }
  }

  if (min_indent == std::string_view::npos || min_indent == 0) {
    return std::string(text);
  }

  std::string result;
  for (size_t idx = 0; idx < lines.size(); ++idx) {
    auto line = lines[idx];
    bool had_cr = (!line.empty() && line.back() == '\r');
    if (had_cr) line.remove_suffix(1);

    if (line.size() >= min_indent) {
      result.append(line.substr(min_indent));
    } else {
      result.append(line);
    }
    if (had_cr) result.push_back('\r');
    if (idx + 1 < lines.size() || text.back() == '\n') {
      result.push_back('\n');
    }
  }

  return result;
}

std::string reindent(std::string_view text, std::string_view indent_prefix) {
  std::string dedented = dedent(text);
  if (dedented.empty()) return "";

  std::string_view view = dedented;
  std::vector<std::string_view> lines;
  size_t start = 0;
  while (start < view.size()) {
    size_t end = view.find('\n', start);
    if (end == std::string_view::npos) {
      lines.push_back(view.substr(start));
      break;
    }
    lines.push_back(view.substr(start, end - start));
    start = end + 1;
  }

  std::string result;
  for (size_t idx = 0; idx < lines.size(); ++idx) {
    auto line = lines[idx];
    bool had_cr = (!line.empty() && line.back() == '\r');
    if (had_cr) line.remove_suffix(1);

    if (!line.empty()) {
      result.append(indent_prefix);
      result.append(line);
    }
    if (had_cr) result.push_back('\r');
    if (idx + 1 < lines.size() || dedented.back() == '\n') {
      result.push_back('\n');
    }
  }

  return result;
}

std::string to_crlf(std::string_view text) {
  std::string res;
  res.reserve(text.size() + text.size() / 20);
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      continue;
    }
    if (text[i] == '\n') {
      res.push_back('\r');
      res.push_back('\n');
    } else {
      res.push_back(text[i]);
    }
  }
  return res;
}

std::string to_lf(std::string_view text) {
  std::string res;
  res.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      continue;
    }
    res.push_back(text[i]);
  }
  return res;
}

LineIndex::LineIndex(std::string_view text) : total_bytes_(text.size()) {
  line_starts_.push_back(0);
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      line_starts_.push_back(i + 1);
    }
  }
}

Position LineIndex::position_at(size_t byte_offset) const {
  if (line_starts_.empty() || byte_offset == 0) {
    return {1, 1};
  }
  if (byte_offset > total_bytes_) {
    byte_offset = total_bytes_;
  }

  auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), byte_offset);
  size_t line_idx = std::distance(line_starts_.begin(), it) - 1;
  size_t col_idx = byte_offset - line_starts_[line_idx];

  return {static_cast<uint32_t>(line_idx + 1), static_cast<uint32_t>(col_idx + 1)};
}

size_t LineIndex::offset_at(uint32_t line, uint32_t col) const {
  if (line == 0 || line > line_starts_.size()) return total_bytes_;
  size_t line_start = line_starts_[line - 1];
  size_t offset = line_start + (col > 0 ? col - 1 : 0);
  return std::min(offset, total_bytes_);
}

} // namespace gs::core
