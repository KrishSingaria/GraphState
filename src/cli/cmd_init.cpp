#include "cli/cli.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "host_kits.inc"
#include "platform/platform.hpp"

namespace gs::cli {

namespace {

std::string replace_all(std::string str, std::string_view from, std::string_view to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
  return str;
}

void deep_merge(nlohmann::json& target, const nlohmann::json& patch) {
  if (patch.is_object()) {
    if (!target.is_object()) {
      target = nlohmann::json::object();
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
      if (target.contains(it.key())) {
        deep_merge(target[it.key()], it.value());
      } else {
        target[it.key()] = it.value();
      }
    }
  } else if (patch.is_array()) {
    if (!target.is_array()) {
      target = nlohmann::json::array();
    }
    for (const auto& item : patch) {
      bool found = false;
      for (const auto& existing : target) {
        if (existing == item) {
          found = true;
          break;
        }
      }
      if (!found) {
        target.push_back(item);
      }
    }
  } else {
    target = patch;
  }
}

void update_gitignore(const std::filesystem::path& root) {
  std::filesystem::path gitignore_path = root / ".gitignore";
  std::string content;
  if (std::filesystem::exists(gitignore_path)) {
    std::ifstream ifs(gitignore_path);
    if (ifs.is_open()) {
      content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
  }

  bool has_wal = (content.find(".graphstate/wal") != std::string::npos);
  bool has_logs = (content.find(".graphstate/logs") != std::string::npos);

  if (!has_wal || !has_logs) {
    std::ofstream ofs(gitignore_path, std::ios::app);
    if (!content.empty() && content.back() != '\n') {
      ofs << "\n";
    }
    if (!has_wal) {
      ofs << ".graphstate/wal/\n";
    }
    if (!has_logs) {
      ofs << ".graphstate/logs/\n";
    }
    std::cout << "  [OK] Updated .gitignore with GraphState patterns\n";
  }
}

} // namespace

int cmd_init(int argc, char* argv[]) {
  std::string host = "claude-code";
  std::filesystem::path root = std::filesystem::current_path();
  bool force = false;

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if ((arg == "--host" || arg == "-h_host") && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--force") {
      force = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate init [options]\n\n"
                << "Options:\n"
                << "  --host <name>   Host to initialize (claude-code, antigravity, cursor, claude-desktop, generic)\n"
                << "  --root <path>   Workspace root (default: current directory)\n"
                << "  --force         Overwrite existing non-JSON files (e.g. AGENTS.md)\n"
                << "  -h, --help      Show this help message\n";
      return 0;
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(root / ".graphstate", ec);

  // Normalize host name
  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) -> char {
    return static_cast<char>(std::tolower(c));
  });

  std::string bin_path = platform::to_utf8(platform::executable_path());
  std::replace(bin_path.begin(), bin_path.end(), '\\', '/');

  auto canon_root = std::filesystem::weakly_canonical(root, ec);
  std::string root_path = platform::to_utf8(canon_root);
  std::replace(root_path.begin(), root_path.end(), '\\', '/');

  if (host == "claude-desktop") {
    nlohmann::json desktop_cfg = {
        {"mcpServers", {
            {"graphstate", {
                {"command", bin_path},
                {"args", {"serve", "--root", root_path}}
            }}
        }}
    };

    std::cout << "\nGraphState initialized for Claude Desktop.\n\n"
              << "Add the following configuration to your claude_desktop_config.json:\n\n"
              << desktop_cfg.dump(2) << "\n\n";
    return 0;
  }

  std::cout << "Initializing GraphState for " << host << " in " << root_path << "...\n";

  update_gitignore(root);

  std::string host_prefix = host + "/";
  bool is_generic = (host == "generic");

  for (const auto& entry : embedded::HOST_KITS) {
    std::string rel_template = std::string(entry.path);
    std::string dest_rel;

    if (rel_template == "AGENTS.md") {
      dest_rel = "AGENTS.md";
    } else if (rel_template.starts_with(host_prefix)) {
      dest_rel = rel_template.substr(host_prefix.size());
    } else if (is_generic && rel_template.starts_with("claude-code/")) {
      dest_rel = rel_template.substr(std::string("claude-code/").size());
    } else {
      continue;
    }

    std::string text = std::string(entry.content);

    // Placeholder substitution:
    // If bin_path has spaces, quote it for hook command invocations
    std::string bin_cmd = (bin_path.find(' ') != std::string::npos) ? ("\\\"" + bin_path + "\\\"") : bin_path;
    text = replace_all(text, "{{GRAPHSTATE_BIN}} hook", bin_cmd + " hook");
    text = replace_all(text, "{{GRAPHSTATE_BIN}}", bin_path);
    text = replace_all(text, "{{ROOT}}", root_path);

    std::filesystem::path dest_path = root / platform::from_utf8(dest_rel);
    std::filesystem::create_directories(dest_path.parent_path(), ec);

    bool is_json = dest_path.filename().string().ends_with(".json");

    if (is_json && std::filesystem::exists(dest_path, ec)) {
      try {
        nlohmann::json existing_json;
        std::ifstream ifs(dest_path);
        if (ifs.is_open()) {
          existing_json = nlohmann::json::parse(ifs);
        }
        ifs.close();

        nlohmann::json patch_json = nlohmann::json::parse(text);
        deep_merge(existing_json, patch_json);

        std::ofstream ofs(dest_path);
        ofs << existing_json.dump(2) << "\n";
        std::cout << "  [OK] Merged " << dest_rel << "\n";
      } catch (const std::exception& e) {
        std::ofstream ofs(dest_path);
        ofs << text;
        std::cout << "  [OK] Wrote " << dest_rel << " (" << e.what() << ")\n";
      }
    } else if (!is_json && std::filesystem::exists(dest_path, ec) && !force) {
      std::cout << "  [SKIP] " << dest_rel << " already exists (use --force to overwrite)\n";
    } else {
      std::ofstream ofs(dest_path, std::ios::binary);
      ofs << text;
      std::cout << "  [OK] Created " << dest_rel << "\n";
    }
  }

  std::cout << "\nInitialization complete!\n\n";
  if (host == "claude-code") {
    std::cout << "Next steps:\n"
              << "  1. Start Claude Code: claude\n"
              << "  2. Direct edits are denied by .claude/settings.json hooks.\n"
              << "  3. Start with: \"Read AGENTS.md and call mcp__graphstate__get_briefing\".\n";
  } else if (host == "antigravity") {
    std::cout << "Next steps:\n"
              << "  1. Open this workspace in Google Antigravity.\n"
              << "  2. MCP server is configured in .agents/mcp_config.json.\n"
              << "  3. Direct edits are blocked by .agents/hooks.json.\n";
  } else if (host == "cursor") {
    std::cout << "Next steps:\n"
              << "  1. Open this workspace in Cursor.\n"
              << "  2. MCP server is configured in .cursor/mcp.json.\n"
              << "  3. Direct edits are blocked by .cursor/hooks.json.\n";
  }

  return 0;
}

} // namespace gs::cli
