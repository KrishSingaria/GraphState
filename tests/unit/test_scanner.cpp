#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include "core/config.hpp"
#include "index/scanner.hpp"
#include "platform/platform.hpp"

using namespace gs;
using namespace gs::index;

namespace {

struct TempDirectory {
  std::filesystem::path path;

  TempDirectory(std::string_view name) {
    path = std::filesystem::temp_directory_path() / ("gs_test_" + std::string(name));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }

  ~TempDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  void write_file(std::string_view rel, std::string_view content) {
    auto p = path / platform::from_utf8(rel);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  }

  void write_binary_file(std::string_view rel, size_t size, size_t nul_offset) {
    auto p = path / platform::from_utf8(rel);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary);
    for (size_t i = 0; i < size; ++i) {
      char c = (i == nul_offset) ? '\0' : 'A';
      ofs.put(c);
    }
  }
};

} // namespace

TEST_CASE("is_binary_file detection accuracy") {
  TempDirectory tmp("scanner_binary");

  SUBCASE("Empty file is not binary") {
    tmp.write_file("empty.py", "");
    CHECK_FALSE(is_binary_file(tmp.path / "empty.py"));
  }

  SUBCASE("Valid ASCII text file is not binary") {
    tmp.write_file("code.py", "def add(a, b):\n    return a + b\n");
    CHECK_FALSE(is_binary_file(tmp.path / "code.py"));
  }

  SUBCASE("Valid UTF-8 with multi-byte chars is not binary") {
    tmp.write_file("utf8.py", "# Greek: αβγ, Japanese: こんにちは, Emoji: 🚀\nmsg = 'hello'\n");
    CHECK_FALSE(is_binary_file(tmp.path / "utf8.py"));
  }

  SUBCASE("File with NUL at index 0 is binary") {
    tmp.write_binary_file("nul_zero.bin", 128, 0);
    CHECK(is_binary_file(tmp.path / "nul_zero.bin"));
  }

  SUBCASE("File with NUL within 8 KiB window is binary") {
    tmp.write_binary_file("nul_mid.bin", 8192, 4096);
    CHECK(is_binary_file(tmp.path / "nul_mid.bin"));
  }

  SUBCASE("File with NUL at byte 8191 (last byte of window) is binary") {
    tmp.write_binary_file("nul_edge.bin", 10000, 8191);
    CHECK(is_binary_file(tmp.path / "nul_edge.bin"));
  }

  SUBCASE("File with NUL beyond 8 KiB window is treated as text") {
    tmp.write_binary_file("nul_past.bin", 10000, 8192);
    CHECK_FALSE(is_binary_file(tmp.path / "nul_past.bin"));
  }

  SUBCASE("Non-existent file returns false") {
    CHECK_FALSE(is_binary_file(tmp.path / "non_existent.file"));
  }
}

TEST_CASE("Scanner fallback recursive walk with pruning and size thresholds") {
  TempDirectory tmp("scanner_fallback");

  // Create workspace layout
  tmp.write_file("src/app.py", "import os\n");
  tmp.write_file("src/utils.py", "def util(): pass\n");
  tmp.write_file("src/app.min.js", "function a(){}");
  tmp.write_file("node_modules/pkg/index.js", "module.exports={};");
  tmp.write_file(".venv/lib/site.py", "site=1");
  tmp.write_binary_file("src/logo.png", 500, 10);
  tmp.write_file("large.py", std::string(2048, '#'));

  core::Config config;
  config.root = tmp.path;
  config.index.use_git_ls_files = false; // Force fallback
  config.index.max_file_bytes = 1024;    // 1 KiB threshold
  config.index.ignore = {
      "**/node_modules/**",
      "**/.venv/**",
      "**/*.min.js"
  };

  auto scanned = scan(config, tmp.path);

  // Expecting exactly 2 files: src/app.py and src/utils.py
  REQUIRE(scanned.size() == 2);
  CHECK(scanned[0].rel == "src/app.py");
  CHECK(scanned[0].size > 0);
  CHECK(scanned[0].mtime_ns > 0);
  CHECK(scanned[1].rel == "src/utils.py");
}

TEST_CASE("Scanner Git fast path honors .gitignore and excludes binaries") {
  TempDirectory tmp("scanner_git");

  // Initialize git repository
  auto r_init = platform::run_capture({"git", "init"}, tmp.path);
  if (r_init.exit_code != 0) {
    return;
  }
  platform::run_capture({"git", "config", "user.name", "Test"}, tmp.path);
  platform::run_capture({"git", "config", "user.email", "test@test.com"}, tmp.path);

  // Create files
  tmp.write_file("tracked.py", "tracked = True\n");
  tmp.write_file("untracked.ts", "export const x = 1;\n");
  tmp.write_file(".gitignore", "ignored/\n*.tmp\n");
  tmp.write_file("ignored/bad.py", "secret = 1\n");
  tmp.write_file("temp.tmp", "temp = 1\n");
  tmp.write_binary_file("blob.bin", 256, 5);
  tmp.write_file("huge.py", std::string(2048, 'X'));

  platform::run_capture({"git", "add", "tracked.py", ".gitignore"}, tmp.path);
  platform::run_capture({"git", "commit", "-m", "init"}, tmp.path);

  core::Config config;
  config.root = tmp.path;
  config.index.use_git_ls_files = true;
  config.index.max_file_bytes = 1000;
  config.index.ignore = {"**/.git/**"};

  auto scanned = scan(config, tmp.path);

  std::vector<std::string> paths;
  for (const auto& s : scanned) paths.push_back(s.rel);

  CHECK(std::find(paths.begin(), paths.end(), "tracked.py") != paths.end());
  CHECK(std::find(paths.begin(), paths.end(), "untracked.ts") != paths.end());
  CHECK(std::find(paths.begin(), paths.end(), "ignored/bad.py") == paths.end());
  CHECK(std::find(paths.begin(), paths.end(), "temp.tmp") == paths.end());
  CHECK(std::find(paths.begin(), paths.end(), "blob.bin") == paths.end());
  CHECK(std::find(paths.begin(), paths.end(), "huge.py") == paths.end());
}
