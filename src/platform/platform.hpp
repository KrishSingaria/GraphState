#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "core/error.hpp"

namespace gs::platform {

namespace fs = std::filesystem;

// ── stdio & lifecycle ────────────────────────────────────────────────
void init_stdio();
void on_shutdown_request(std::function<void()> cb);
[[noreturn]] void hard_exit(int code);

// ── paths & strings ──────────────────────────────────────────────────
std::string to_utf8(const fs::path& p);
fs::path from_utf8(std::string_view s);
fs::path executable_path();
bool fs_case_insensitive(const fs::path& dir);

// ── files ────────────────────────────────────────────────────────────
struct FileStat {
  bool exists = false;
  bool regular = false;
  uint64_t size = 0;
  int64_t mtime_ns = 0;
};

FileStat stat(const fs::path& p);
void write_file_durable(const fs::path& p, std::string_view bytes, bool sync = true);
void replace_file(const fs::path& src_tmp, const fs::path& dst, bool write_through = true);
void remove_file(const fs::path& p);
void fsync_dir(const fs::path& dir);
bool hardlink_or_copy(const fs::path& from, const fs::path& to);

struct FileMode {
  uint32_t posix_mode = 0644;
  bool readonly = false;
};

FileMode file_mode(const fs::path& p);
void set_file_mode(const fs::path& p, const FileMode& mode);

// ── processes ────────────────────────────────────────────────────────
struct ProcResult {
  int exit_code = -1;
  std::string out;
  std::string err;
  bool timed_out = false;
};

ProcResult run_capture(const std::vector<std::string>& argv, const fs::path& cwd, int timeout_ms = 30000);

// ── misc ─────────────────────────────────────────────────────────────
uint64_t peak_rss_bytes();
std::string os_name();

} // namespace gs::platform
