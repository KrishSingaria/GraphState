#include "cli/cli.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

#include "platform/platform.hpp"

namespace gs::cli {

namespace {

bool is_path_inside_workspace(const std::string& target_file_str, const std::filesystem::path& root) {
  if (target_file_str.empty()) {
    std::error_code ec;
    return std::filesystem::exists(root / ".graphstate", ec);
  }

  std::filesystem::path target_p = platform::from_utf8(target_file_str);
  if (target_p.is_relative()) {
    target_p = root / target_p;
  }

  std::error_code ec;
  auto canon_target = std::filesystem::weakly_canonical(target_p, ec);
  auto canon_root = std::filesystem::weakly_canonical(root, ec);

  std::string s_target = platform::to_utf8(canon_target);
  std::string s_root = platform::to_utf8(canon_root);
  std::replace(s_target.begin(), s_target.end(), '\\', '/');
  std::replace(s_root.begin(), s_root.end(), '\\', '/');

  if (platform::fs_case_insensitive(root)) {
    std::string st_lower = s_target;
    std::string sr_lower = s_root;
    std::transform(st_lower.begin(), st_lower.end(), st_lower.begin(), [](unsigned char c) -> char {
      return static_cast<char>(std::tolower(c));
    });
    std::transform(sr_lower.begin(), sr_lower.end(), sr_lower.begin(), [](unsigned char c) -> char {
      return static_cast<char>(std::tolower(c));
    });
    return st_lower.starts_with(sr_lower);
  }

  return s_target.starts_with(s_root);
}

} // namespace

int cmd_hook(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: graphstate hook <subcommand> [options]\n\n"
              << "Subcommands:\n"
              << "  deny-edit       PreToolUse hook interceptor to block direct file edits\n";
    return 1;
  }

  std::string_view subcommand = argv[2];
  if (subcommand != "deny-edit") {
    std::cerr << "Unknown hook subcommand: " << subcommand << "\n";
    return 1;
  }

  std::string host = "claude-code";
  std::filesystem::path root = std::filesystem::current_path();

  for (int i = 3; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate hook deny-edit [options]\n\n"
                << "Options:\n"
                << "  --host <name>   Host format (claude-code, antigravity, cursor)\n"
                << "  --root <path>   Workspace root (default: current directory)\n"
                << "  -h, --help      Show this help message\n";
      return 0;
    }
  }

  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) -> char {
    return static_cast<char>(std::tolower(c));
  });

  // Read hook input from stdin
  std::string input;
  std::string line;
  while (std::getline(std::cin, line)) {
    input += line;
    input += '\n';
  }

  std::string target_file;
  try {
    if (!input.empty()) {
      auto j = nlohmann::json::parse(input);
      nlohmann::json tool_input;

      if (j.contains("tool_input") && j["tool_input"].is_object()) {
        tool_input = j["tool_input"];
      } else if (j.contains("arguments") && j["arguments"].is_object()) {
        tool_input = j["arguments"];
      } else if (j.is_object()) {
        tool_input = j;
      }

      const char* fields[] = {
          "file_path", "notebook_path", "TargetFile", "target_file", "targetFile", "path", "file"
      };
      for (const char* field : fields) {
        if (tool_input.contains(field) && tool_input[field].is_string()) {
          target_file = tool_input[field].get<std::string>();
          break;
        }
      }
    }
  } catch (...) {}

  bool inside = is_path_inside_workspace(target_file, root);

  if (inside) {
    if (host == "antigravity") {
      nlohmann::json res = {
          {"decision", "deny"},
          {"reason", "GraphState: direct edits are disabled in this repository. Use the graphstate MCP tool propose_patch (ops: replace_symbol, insert_symbol, delete_symbol, replace_text, create_file, delete_file). Read the target first with read_symbol or get_file_outline."}
      };
      std::cout << res.dump() << "\n";
      return 0;
    } else if (host == "cursor") {
      nlohmann::json res = {
          {"permission", "deny"},
          {"agent_message", "GraphState: direct edits are disabled in this repository. Use propose_patch (ops: replace_symbol, insert_symbol, delete_symbol, replace_text, create_file, delete_file). Read the target first with read_symbol or get_file_outline."},
          {"user_message", "Blocked a direct edit; GraphState gate is active."}
      };
      std::cout << res.dump() << "\n";
      return 0;
    } else {
      // Default: claude-code
      std::cerr << "GraphState: direct edits are disabled in this repository. Use mcp__graphstate__propose_patch (ops: replace_symbol, insert_symbol, delete_symbol, replace_text, create_file, delete_file). Read the target first with mcp__graphstate__read_symbol or get_file_outline.\n";
      return 2;
    }
  }

  // Outside workspace: allow
  return 0;
}

} // namespace gs::cli
