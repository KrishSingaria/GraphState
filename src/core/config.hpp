#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace gs::core {

struct FalkorDbConfig {
  std::string host = "127.0.0.1";
  int port = 6390;
  std::string password = "";
  int connect_timeout_ms = 1000;
  int query_timeout_ms = 10000;
  std::string graph_prefix = "gs";
};

struct IndexConfig {
  uint64_t max_file_bytes = 1048576; // 1 MiB
  std::vector<std::string> ignore = {
    "**/node_modules/**",
    "**/.git/**",
    "**/.venv/**",
    "**/venv/**",
    "**/__pycache__/**",
    "**/dist/**",
    "**/build/**",
    "**/target/**",
    "**/.graphstate/**",
    "**/*.min.js"
  };
  bool use_git_ls_files = true;
  int parse_threads = 0; // 0 -> auto min(8, hardware_concurrency())
};

struct PythonConfig {
  std::vector<std::string> source_roots = {"", "src"};
};

struct WatchConfig {
  bool enabled = true;
  int interval_ms = 500;
  int rescan_interval_ms = 10000;
  int bulk_threshold = 20;
};

struct GateConfig {
  std::string mode = "enforce";
  std::vector<std::string> protected_patterns = {
    ".git/**",
    ".graphstate/**",
    "**/.env",
    "**/.env.*",
    "**/*.lock",
    "**/package-lock.json",
    "**/node_modules/**"
  };
  std::string unknown_files = "allow";
  bool allow_write_without_db = false;
  int max_edits = 200;
  int max_files = 100;
  uint64_t max_code_bytes = 1048576;
  uint64_t max_patch_bytes = 8388608;
  std::string new_file_eol = "auto";
};

struct MemoryConfig {
  int session_timeout_s = 120;
  int briefing_max_items = 15;
  std::string user_graph = "gs:__user__";
};

struct LogConfig {
  std::string level = "info";
  std::string file = ".graphstate/logs/engine.log";
  uint64_t max_bytes = 10485760;
  int keep = 3;
};

struct Config {
  std::filesystem::path root;
  FalkorDbConfig falkordb;
  IndexConfig index;
  PythonConfig python;
  WatchConfig watch;
  GateConfig gate;
  MemoryConfig memory;
  LogConfig log;

  std::string workspace_key() const;
  std::string project_graph() const;
  std::string user_graph() const;
  int effective_parse_threads() const;

  static Config load(const std::filesystem::path& root,
                     const std::filesystem::path& explicit_config = {});
};

} // namespace gs::core
