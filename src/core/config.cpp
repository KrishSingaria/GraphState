#include "core/config.hpp"
#include "core/hash.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <thread>

namespace gs::core {

namespace {

void merge_json_into_config(const nlohmann::json& j, Config& cfg) {
  if (j.contains("falkordb") && j["falkordb"].is_object()) {
    const auto& f = j["falkordb"];
    if (f.contains("host") && f["host"].is_string()) cfg.falkordb.host = f["host"];
    if (f.contains("port") && f["port"].is_number_integer()) cfg.falkordb.port = f["port"];
    if (f.contains("password") && f["password"].is_string()) cfg.falkordb.password = f["password"];
    if (f.contains("graph_prefix") && f["graph_prefix"].is_string()) cfg.falkordb.graph_prefix = f["graph_prefix"];
    if (f.contains("connect_timeout_ms") && f["connect_timeout_ms"].is_number_integer()) cfg.falkordb.connect_timeout_ms = f["connect_timeout_ms"];
    if (f.contains("query_timeout_ms") && f["query_timeout_ms"].is_number_integer()) cfg.falkordb.query_timeout_ms = f["query_timeout_ms"];
  }
  if (j.contains("index") && j["index"].is_object()) {
    const auto& idx = j["index"];
    if (idx.contains("max_file_bytes") && idx["max_file_bytes"].is_number()) {
      cfg.index.max_file_bytes = idx["max_file_bytes"].get<uint64_t>();
    }
    if (idx.contains("use_git_ls_files") && idx["use_git_ls_files"].is_boolean()) {
      cfg.index.use_git_ls_files = idx["use_git_ls_files"];
    }
    if (idx.contains("parse_threads") && idx["parse_threads"].is_number_integer()) {
      cfg.index.parse_threads = idx["parse_threads"];
    }
    if (idx.contains("ignore") && idx["ignore"].is_array()) {
      cfg.index.ignore.clear();
      for (const auto& item : idx["ignore"]) {
        if (item.is_string()) cfg.index.ignore.push_back(item);
      }
    }
  }
  if (j.contains("python") && j["python"].is_object()) {
    const auto& py = j["python"];
    if (py.contains("source_roots") && py["source_roots"].is_array()) {
      cfg.python.source_roots.clear();
      for (const auto& item : py["source_roots"]) {
        if (item.is_string()) cfg.python.source_roots.push_back(item);
      }
    }
  }
  if (j.contains("watch") && j["watch"].is_object()) {
    const auto& w = j["watch"];
    if (w.contains("enabled") && w["enabled"].is_boolean()) cfg.watch.enabled = w["enabled"];
    if (w.contains("interval_ms") && w["interval_ms"].is_number_integer()) cfg.watch.interval_ms = w["interval_ms"];
    if (w.contains("rescan_interval_ms") && w["rescan_interval_ms"].is_number_integer()) cfg.watch.rescan_interval_ms = w["rescan_interval_ms"];
    if (w.contains("bulk_threshold") && w["bulk_threshold"].is_number_integer()) cfg.watch.bulk_threshold = w["bulk_threshold"];
  }
  if (j.contains("gate") && j["gate"].is_object()) {
    const auto& g = j["gate"];
    if (g.contains("mode") && g["mode"].is_string()) cfg.gate.mode = g["mode"];
    if (g.contains("unknown_files") && g["unknown_files"].is_string()) cfg.gate.unknown_files = g["unknown_files"];
    if (g.contains("allow_write_without_db") && g["allow_write_without_db"].is_boolean()) cfg.gate.allow_write_without_db = g["allow_write_without_db"];
    if (g.contains("max_edits") && g["max_edits"].is_number_integer()) cfg.gate.max_edits = g["max_edits"];
    if (g.contains("max_files") && g["max_files"].is_number_integer()) cfg.gate.max_files = g["max_files"];
    if (g.contains("max_code_bytes") && g["max_code_bytes"].is_number()) cfg.gate.max_code_bytes = g["max_code_bytes"].get<uint64_t>();
    if (g.contains("max_patch_bytes") && g["max_patch_bytes"].is_number()) cfg.gate.max_patch_bytes = g["max_patch_bytes"].get<uint64_t>();
    if (g.contains("new_file_eol") && g["new_file_eol"].is_string()) cfg.gate.new_file_eol = g["new_file_eol"];
    if (g.contains("protected_patterns") && g["protected_patterns"].is_array()) {
      cfg.gate.protected_patterns.clear();
      for (const auto& item : g["protected_patterns"]) {
        if (item.is_string()) cfg.gate.protected_patterns.push_back(item);
      }
    }
  }
  if (j.contains("memory") && j["memory"].is_object()) {
    const auto& m = j["memory"];
    if (m.contains("session_timeout_s") && m["session_timeout_s"].is_number_integer()) cfg.memory.session_timeout_s = m["session_timeout_s"];
    if (m.contains("briefing_max_items") && m["briefing_max_items"].is_number_integer()) cfg.memory.briefing_max_items = m["briefing_max_items"];
    if (m.contains("user_graph") && m["user_graph"].is_string()) cfg.memory.user_graph = m["user_graph"];
  }
  if (j.contains("log") && j["log"].is_object()) {
    const auto& l = j["log"];
    if (l.contains("level") && l["level"].is_string()) cfg.log.level = l["level"];
    if (l.contains("file") && l["file"].is_string()) cfg.log.file = l["file"];
    if (l.contains("max_bytes") && l["max_bytes"].is_number()) cfg.log.max_bytes = l["max_bytes"].get<uint64_t>();
    if (l.contains("keep") && l["keep"].is_number_integer()) cfg.log.keep = l["keep"];
  }
}

void try_load_file(const std::filesystem::path& path, Config& cfg) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
    return;
  }
  std::ifstream f(path);
  if (!f.is_open()) {
    return;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
      merge_json_into_config(j, cfg);
    }
  } catch (...) {
    // Ignore malformed config file, retain existing settings
  }
}

} // namespace

std::string Config::workspace_key() const {
  std::filesystem::path norm_root = root.lexically_normal();
  std::error_code ec;
  std::filesystem::path real_p = std::filesystem::exists(norm_root, ec)
      ? std::filesystem::canonical(norm_root, ec)
      : std::filesystem::weakly_canonical(norm_root, ec);

  std::string real_str = platform::to_utf8(real_p);
  for (char& c : real_str) {
    if (c == '\\') c = '/';
  }

  if (real_str.starts_with("//?/") || real_str.starts_with("\\\\?\\")) {
    real_str = real_str.substr(4);
  }

  while (real_str.size() > 1 && real_str.back() == '/') {
    if (real_str.size() == 3 && real_str[1] == ':') {
      break;
    }
    real_str.pop_back();
  }

  std::string hash_suffix = xxh3_hex(real_str).substr(0, 8);

  std::string base = norm_root.filename().string();
  if (base.empty()) {
    base = norm_root.parent_path().filename().string();
  }
  if (base.empty()) {
    base = "workspace";
  }

  std::string sanitized;
  sanitized.reserve(base.size());
  for (char c : base) {
    char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') || lc == '_' || lc == '-') {
      sanitized += lc;
    } else {
      sanitized += '_';
    }
  }
  if (sanitized.size() > 32) {
    sanitized = sanitized.substr(0, 32);
  }
  if (sanitized.empty()) {
    sanitized = "workspace";
  }

  return sanitized + "-" + hash_suffix;
}

std::string Config::project_graph() const {
  return falkordb.graph_prefix + ":" + workspace_key();
}

std::string Config::user_graph() const {
  return memory.user_graph;
}

int Config::effective_parse_threads() const {
  if (index.parse_threads > 0) {
    return index.parse_threads;
  }
  unsigned int hw = std::thread::hardware_concurrency();
  if (hw == 0) hw = 4;
  return static_cast<int>(std::min(8u, hw));
}

Config Config::load(const std::filesystem::path& root,
                    const std::filesystem::path& explicit_config) {
  Config cfg;
  cfg.root = root;

  // 1. User global config (~/.config/graphstate/config.json)
  const char* home = std::getenv("USERPROFILE");
  if (!home) home = std::getenv("HOME");
  if (home) {
    std::filesystem::path user_cfg = std::filesystem::path(home) / ".config" / "graphstate" / "config.json";
    try_load_file(user_cfg, cfg);
  }

  // 2. Workspace config
  if (!explicit_config.empty()) {
    try_load_file(explicit_config, cfg);
  } else {
    try_load_file(root / ".graphstate" / "config.json", cfg);
  }

  // 3. Environment variable overrides
  if (const char* v = std::getenv("GRAPHSTATE_ROOT")) {
    if (cfg.root.empty()) {
      cfg.root = platform::from_utf8(v);
    }
  }
  if (const char* v = std::getenv("GRAPHSTATE_FALKOR_HOST")) {
    if (*v != '\0') cfg.falkordb.host = v;
  }
  if (const char* v = std::getenv("GRAPHSTATE_FALKOR_PORT")) {
    if (*v != '\0') {
      try {
        cfg.falkordb.port = std::stoi(v);
      } catch (...) {}
    }
  }
  if (const char* v = std::getenv("GRAPHSTATE_FALKOR_PASSWORD")) {
    cfg.falkordb.password = v;
  }
  if (const char* v = std::getenv("GRAPHSTATE_LOG_LEVEL")) {
    if (*v != '\0') cfg.log.level = v;
  }
  if (const char* v = std::getenv("GRAPHSTATE_GATE_MODE")) {
    if (*v != '\0') cfg.gate.mode = v;
  }
  if (const char* v = std::getenv("GRAPHSTATE_NO_WATCH")) {
    std::string s(v);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "1" || s == "true" || s == "yes") {
      cfg.watch.enabled = false;
    }
  }

  return cfg;
}

} // namespace gs::core
