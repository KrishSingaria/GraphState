#include "index/scanner.hpp"
#include "core/paths.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace gs::index {

bool is_binary_file(const std::filesystem::path& abs_path) {
  std::ifstream file(abs_path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  char buffer[8192];
  file.read(buffer, sizeof(buffer));
  std::streamsize bytes_read = file.gcount();

  for (std::streamsize i = 0; i < bytes_read; ++i) {
    if (buffer[i] == '\0') {
      return true;
    }
  }
  return false;
}

namespace {

bool matches_any_ignore(std::string_view rel_path,
                        const std::vector<std::string>& patterns,
                        bool case_insensitive) {
  for (const auto& pattern : patterns) {
    if (core::glob_match(pattern, rel_path, case_insensitive)) {
      return true;
    }
  }
  return false;
}

bool is_ignored_directory(const std::string& rel_dir,
                          const std::vector<std::string>& patterns,
                          bool case_insensitive) {
  for (const auto& pattern : patterns) {
    if (core::glob_match(pattern, rel_dir, case_insensitive)) return true;
    if (core::glob_match(pattern, rel_dir + "/", case_insensitive)) return true;
    if (core::glob_match(pattern, rel_dir + "/placeholder", case_insensitive)) return true;
  }
  return false;
}

void process_file_candidate(const std::filesystem::path& root,
                            std::string rel_path,
                            const core::Config& config,
                            bool case_insensitive,
                            std::vector<ScannedFile>& out) {
  for (char& c : rel_path) {
    if (c == '\\') c = '/';
  }

  // 1. Check ignore patterns
  if (matches_any_ignore(rel_path, config.index.ignore, case_insensitive)) {
    return;
  }

  // 2. Stat check
  std::filesystem::path abs_path = root / platform::from_utf8(rel_path);
  auto st = platform::stat(abs_path);
  if (!st.exists || !st.regular) {
    return;
  }

  // 3. File size check (drop files > max_file_bytes)
  if (st.size > config.index.max_file_bytes) {
    return;
  }

  // 4. Binary check (drop files with NUL byte in first 8 KiB)
  if (is_binary_file(abs_path)) {
    return;
  }

  out.push_back(ScannedFile{
      .rel = std::move(rel_path),
      .size = st.size,
      .mtime_ns = st.mtime_ns,
  });
}

bool try_git_fast_path(const core::Config& config,
                       const std::filesystem::path& root,
                       bool case_insensitive,
                       std::vector<ScannedFile>& out) {
  std::filesystem::path git_dir = root / ".git";
  std::error_code ec;
  if (!std::filesystem::exists(git_dir, ec)) {
    return false;
  }

  std::vector<std::string> argv = {
      "git", "-C", platform::to_utf8(root), "ls-files", "-co", "--exclude-standard", "-z"
  };

  auto res = platform::run_capture(argv, root, 10000);
  if (res.exit_code != 0 || res.timed_out) {
    return false;
  }

  size_t start = 0;
  while (start < res.out.size()) {
    size_t nul = res.out.find('\0', start);
    std::string_view path_view;
    if (nul == std::string::npos) {
      path_view = std::string_view(res.out).substr(start);
      start = res.out.size();
    } else {
      path_view = std::string_view(res.out).substr(start, nul - start);
      start = nul + 1;
    }

    if (path_view.empty()) {
      continue;
    }

    process_file_candidate(root, std::string(path_view), config, case_insensitive, out);
  }

  return true;
}

void run_filesystem_fallback(const core::Config& config,
                             const std::filesystem::path& root,
                             bool case_insensitive,
                             std::vector<ScannedFile>& out) {
  std::error_code ec;
  auto it = std::filesystem::recursive_directory_iterator(
      root, std::filesystem::directory_options::none, ec);
  auto end = std::filesystem::recursive_directory_iterator();

  while (it != end && !ec) {
    const auto& entry = *it;
    std::string rel = core::to_workspace_rel(root, entry.path());

    if (entry.is_directory(ec)) {
      if (is_ignored_directory(rel, config.index.ignore, case_insensitive)) {
        it.disable_recursion_pending();
      }
      it.increment(ec);
      continue;
    }

    if (entry.is_regular_file(ec)) {
      process_file_candidate(root, std::move(rel), config, case_insensitive, out);
    }

    it.increment(ec);
  }
}

} // namespace

std::vector<ScannedFile> scan(const core::Config& config, const std::filesystem::path& root) {
  std::vector<ScannedFile> files;
  bool case_insensitive = platform::fs_case_insensitive(root);

  bool git_ok = false;
  if (config.index.use_git_ls_files) {
    git_ok = try_git_fast_path(config, root, case_insensitive, files);
  }

  if (!git_ok) {
    files.clear();
    run_filesystem_fallback(config, root, case_insensitive, files);
  }

  std::sort(files.begin(), files.end(), [](const ScannedFile& a, const ScannedFile& b) {
    return a.rel < b.rel;
  });

  return files;
}

} // namespace gs::index
