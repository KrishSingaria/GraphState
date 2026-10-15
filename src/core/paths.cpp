#include "core/paths.hpp"
#include "core/error.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

namespace gs::core {

namespace {

bool char_matches(char pc, char sc, bool case_insensitive) {
  if (case_insensitive) {
    return std::tolower(static_cast<unsigned char>(pc)) ==
           std::tolower(static_cast<unsigned char>(sc));
  }
  return pc == sc;
}

bool match_segment(std::string_view pat, std::string_view str, bool case_insensitive) {
  size_t p = 0, s = 0;
  size_t star_p = std::string_view::npos;
  size_t match_s = 0;

  while (s < str.size()) {
    if (p < pat.size() && (pat[p] == '?' || char_matches(pat[p], str[s], case_insensitive))) {
      p++;
      s++;
    } else if (p < pat.size() && pat[p] == '*') {
      star_p = p;
      match_s = s;
      p++;
    } else if (star_p != std::string_view::npos) {
      p = star_p + 1;
      match_s++;
      s = match_s;
    } else {
      return false;
    }
  }

  while (p < pat.size() && pat[p] == '*') {
    p++;
  }

  return p == pat.size();
}

std::vector<std::string_view> split_slash(std::string_view s) {
  std::vector<std::string_view> segments;
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find('/', start);
    if (end == std::string_view::npos) {
      end = s.size();
    }
    if (end > start) {
      segments.push_back(s.substr(start, end - start));
    }
    start = end + 1;
  }
  return segments;
}

bool glob_match_single(std::string_view pat, std::string_view rel_path, bool case_insensitive) {
  std::string norm_pat(pat);
  for (char& c : norm_pat) {
    if (c == '\\') c = '/';
  }

  std::string norm_path(rel_path);
  for (char& c : norm_path) {
    if (c == '\\') c = '/';
  }

  if (norm_pat == "**") {
    return true;
  }

  if (norm_pat.empty()) {
    return norm_path.empty();
  }

  auto raw_pat_segs = split_slash(norm_pat);
  auto path_segs = split_slash(norm_path);

  // Deduplicate consecutive "**" segments in pattern
  std::vector<std::string_view> pat_segs;
  pat_segs.reserve(raw_pat_segs.size());
  for (auto seg : raw_pat_segs) {
    if (seg == "**" && !pat_segs.empty() && pat_segs.back() == "**") {
      continue;
    }
    pat_segs.push_back(seg);
  }

  const size_t M = pat_segs.size();
  const size_t N = path_segs.size();

  // 2D DP table: dp[i][j] = does pat_segs[i..M-1] match path_segs[j..N-1]
  // Dimensions: (M + 1) x (N + 1)
  std::vector<uint8_t> dp((M + 1) * (N + 1), 0);
  auto idx = [N](size_t i, size_t j) { return i * (N + 1) + j; };

  // Base case: both exhausted
  dp[idx(M, N)] = 1;

  // Base case: path exhausted, pattern has remaining segments
  for (size_t i = M; i > 0; --i) {
    size_t cur = i - 1;
    if (pat_segs[cur] == "**") {
      dp[idx(cur, N)] = dp[idx(cur + 1, N)];
    } else {
      dp[idx(cur, N)] = 0;
    }
  }

  // Transitions
  for (size_t i = M; i > 0; --i) {
    size_t pi = i - 1;
    for (size_t j = N; j > 0; --j) {
      size_t pj = j - 1;
      if (pat_segs[pi] == "**") {
        dp[idx(pi, pj)] = (dp[idx(pi + 1, pj)] || dp[idx(pi, pj + 1)]) ? 1 : 0;
      } else {
        if (match_segment(pat_segs[pi], path_segs[pj], case_insensitive)) {
          dp[idx(pi, pj)] = dp[idx(pi + 1, pj + 1)];
        } else {
          dp[idx(pi, pj)] = 0;
        }
      }
    }
  }

  return dp[idx(0, 0)] != 0;
}

} // namespace

std::vector<std::string> expand_braces(std::string_view pattern) {
  size_t open_pos = std::string_view::npos;
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{') {
      open_pos = i;
      break;
    }
  }

  if (open_pos == std::string_view::npos) {
    return {std::string(pattern)};
  }

  size_t close_pos = std::string_view::npos;
  std::vector<size_t> comma_positions;
  int depth = 0;
  for (size_t i = open_pos; i < pattern.size(); ++i) {
    if (pattern[i] == '{') {
      depth++;
    } else if (pattern[i] == '}') {
      depth--;
      if (depth == 0) {
        close_pos = i;
        break;
      }
    } else if (pattern[i] == ',' && depth == 1) {
      comma_positions.push_back(i);
    }
  }

  if (close_pos == std::string_view::npos || comma_positions.empty()) {
    std::string prefix(pattern.substr(0, open_pos + 1));
    std::string remainder(pattern.substr(open_pos + 1));
    std::vector<std::string> sub_expanded = expand_braces(remainder);
    std::vector<std::string> result;
    result.reserve(sub_expanded.size());
    for (const auto& sub : sub_expanded) {
      result.push_back(prefix + sub);
    }
    return result;
  }

  std::string_view prefix = pattern.substr(0, open_pos);
  std::string_view suffix = pattern.substr(close_pos + 1);

  std::vector<std::string_view> alternatives;
  size_t start = open_pos + 1;
  for (size_t comma : comma_positions) {
    alternatives.push_back(pattern.substr(start, comma - start));
    start = comma + 1;
  }
  alternatives.push_back(pattern.substr(start, close_pos - start));

  std::vector<std::string> result;
  for (std::string_view alt : alternatives) {
    std::string candidate = std::string(prefix) + std::string(alt) + std::string(suffix);
    std::vector<std::string> sub_expanded = expand_braces(candidate);
    result.insert(result.end(), sub_expanded.begin(), sub_expanded.end());
  }

  return result;
}

bool glob_match(std::string_view pattern, std::string_view rel_path, bool case_insensitive) {
  auto expanded = expand_braces(pattern);
  for (const auto& p : expanded) {
    if (glob_match_single(p, rel_path, case_insensitive)) {
      return true;
    }
  }
  return false;
}

bool is_safe_rel_path(std::string_view rel_path) {
  if (rel_path.empty()) {
    return true;
  }

  static const char* const RESERVED_STEMS[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
  };

  std::string norm(rel_path);
  for (char& c : norm) {
    if (c == '\\') c = '/';
  }

  size_t start = 0;
  while (start < norm.size()) {
    size_t end = norm.find('/', start);
    if (end == std::string::npos) {
      end = norm.size();
    }
    std::string_view seg(norm.data() + start, end - start);
    start = end + 1;

    if (seg.empty()) {
      continue;
    }

    if (seg == ".." || seg == ".") {
      return false;
    }

    if (seg.back() == '.' || seg.back() == ' ') {
      return false;
    }

    for (char c : seg) {
      auto uc = static_cast<unsigned char>(c);
      if (uc <= 31 || uc == 127) {
        return false;
      }
      if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*') {
        return false;
      }
    }

    size_t dot_pos = seg.find('.');
    std::string stem(dot_pos == std::string_view::npos ? seg : seg.substr(0, dot_pos));
    for (char& sc : stem) {
      sc = static_cast<char>(std::toupper(static_cast<unsigned char>(sc)));
    }

    for (const char* r : RESERVED_STEMS) {
      if (stem == r) {
        return false;
      }
    }
  }

  return true;
}

bool is_portable_filename(std::string_view rel_path) {
  return is_safe_rel_path(rel_path);
}

std::string normalize_rel_path(std::string_view raw_path) {
  std::string p(raw_path);
  for (char& c : p) {
    if (c == '\\') c = '/';
  }

  if (p.starts_with("/")) {
    throw Error(ErrorCode::PATH_OUTSIDE_WORKSPACE, "Absolute path not allowed: " + p);
  }

  if (p.size() >= 2 && p[1] == ':') {
    throw Error(ErrorCode::PATH_OUTSIDE_WORKSPACE, "Drive prefix not allowed: " + p);
  }

  if (p.starts_with("//")) {
    throw Error(ErrorCode::PATH_OUTSIDE_WORKSPACE, "UNC path not allowed: " + p);
  }

  std::vector<std::string_view> segments;
  size_t start = 0;
  while (start < p.size()) {
    size_t end = p.find('/', start);
    if (end == std::string::npos) {
      end = p.size();
    }
    std::string_view seg(p.data() + start, end - start);
    start = end + 1;

    if (seg.empty() || seg == ".") {
      continue;
    }

    if (seg == "..") {
      throw Error(ErrorCode::PATH_OUTSIDE_WORKSPACE, "Parent directory traversal '..' not allowed: " + p);
    }

    segments.push_back(seg);
  }

  std::string result;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) result += '/';
    result += segments[i];
  }

  if (!result.empty() && !is_safe_rel_path(result)) {
    throw Error(ErrorCode::PATH_INVALID, "Invalid or unportable path: " + result);
  }

  return result;
}

std::string to_workspace_rel(const std::filesystem::path& root, const std::filesystem::path& target) {
  std::error_code ec;
  auto can_root = std::filesystem::weakly_canonical(root, ec);
  if (ec) can_root = root;
  auto can_target = std::filesystem::weakly_canonical(target, ec);
  if (ec) can_target = target;

  auto rel = std::filesystem::relative(can_target, can_root, ec);
  if (ec || rel.empty()) {
    throw Error(ErrorCode::PATH_OUTSIDE_WORKSPACE, "Target outside workspace");
  }

  std::string s = platform::to_utf8(rel);
  for (char& c : s) {
    if (c == '\\') c = '/';
  }

  if (s.starts_with("//?/") || s.starts_with("\\\\?\\")) {
    s = s.substr(4);
  }

  if (s.starts_with("..") || s.starts_with("/") || (s.size() >= 2 && s[1] == ':')) {
    throw Error(ErrorCode::PATH_OUTSIDE_WORKSPACE, "Target outside workspace: " + s);
  }

  return normalize_rel_path(s);
}

bool is_contained(const std::filesystem::path& root, std::string_view rel_path) {
  if (rel_path.starts_with("..") || rel_path.starts_with("/") || rel_path.find(':') != std::string_view::npos) {
    return false;
  }

  std::error_code ec;
  auto full_target = root / platform::from_utf8(rel_path);
  auto can_root = std::filesystem::weakly_canonical(root, ec);
  if (ec) return false;
  auto can_target = std::filesystem::weakly_canonical(full_target, ec);
  if (ec) return false;

  auto rel = std::filesystem::relative(can_target, can_root, ec);
  if (ec || rel.empty()) return false;

  std::string s = platform::to_utf8(rel);
  for (char& c : s) {
    if (c == '\\') c = '/';
  }

  if (s.starts_with("..") || s.starts_with("/") || (s.size() >= 2 && s[1] == ':')) {
    return false;
  }

  if (std::filesystem::exists(full_target, ec)) {
    auto real_target = std::filesystem::canonical(full_target, ec);
    if (!ec) {
      auto real_root = std::filesystem::canonical(root, ec);
      if (!ec) {
        auto real_rel = std::filesystem::relative(real_target, real_root, ec);
        if (ec || real_rel.empty()) return false;
        std::string rs = platform::to_utf8(real_rel);
        for (char& c : rs) {
          if (c == '\\') c = '/';
        }
        if (rs.starts_with("..") || rs.starts_with("/") || (rs.size() >= 2 && rs[1] == ':')) {
          return false;
        }
      }
    }
  }

  return true;
}

} // namespace gs::core
