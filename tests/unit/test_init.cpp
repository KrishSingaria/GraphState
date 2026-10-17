#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "cli/cli.hpp"
#include "platform/platform.hpp"

using namespace gs;

namespace {

struct TempWorkspace {
  std::filesystem::path root;

  TempWorkspace() {
    root = std::filesystem::temp_directory_path() / ("gs_test_init_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  bool file_exists(const std::string& rel) const {
    std::error_code ec;
    return std::filesystem::exists(root / rel, ec);
  }

  std::string read_file(const std::string& rel) const {
    std::filesystem::path p = root / rel;
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

  void write_file(const std::string& rel, const std::string& content) {
    std::filesystem::path p = root / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << content;
  }
};

} // namespace

TEST_CASE("Init: Claude Code host kit initialization") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);

  std::vector<const char*> args = {
      "graphstate", "init", "--host", "claude-code", "--root", root_str.c_str()
  };

  int rc = cli::cmd_init(static_cast<int>(args.size()), const_cast<char**>(args.data()));
  CHECK(rc == 0);

  CHECK(ws.file_exists(".graphstate"));
  CHECK(ws.file_exists(".mcp.json"));
  CHECK(ws.file_exists(".claude/settings.json"));
  CHECK(ws.file_exists("AGENTS.md"));
  CHECK(ws.file_exists(".gitignore"));

  // Check .gitignore content
  std::string gitignore_text = ws.read_file(".gitignore");
  CHECK(gitignore_text.find(".graphstate/wal/") != std::string::npos);
  CHECK(gitignore_text.find(".graphstate/logs/") != std::string::npos);

  // Check .mcp.json placeholder replacement
  std::string mcp_text = ws.read_file(".mcp.json");
  CHECK(mcp_text.find("{{GRAPHSTATE_BIN}}") == std::string::npos);
  CHECK(mcp_text.find("{{ROOT}}") == std::string::npos);
  auto mcp_json = nlohmann::json::parse(mcp_text);
  CHECK(mcp_json.contains("mcpServers"));
  CHECK(mcp_json["mcpServers"].contains("graphstate"));

  // Check .claude/settings.json
  std::string settings_text = ws.read_file(".claude/settings.json");
  auto settings_json = nlohmann::json::parse(settings_text);
  CHECK(settings_json.contains("permissions"));
  CHECK(settings_json["permissions"].contains("deny"));
  CHECK(settings_json.contains("hooks"));

  SUBCASE("Deep merge preserves existing keys") {
    // Modify settings.json with a user key
    settings_json["userCustomKey"] = "custom_value";
    settings_json["permissions"]["deny"].push_back("CustomTool");
    ws.write_file(".claude/settings.json", settings_json.dump(2));

    // Re-run init
    int rc2 = cli::cmd_init(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc2 == 0);

    std::string merged_text = ws.read_file(".claude/settings.json");
    auto merged_json = nlohmann::json::parse(merged_text);
    CHECK(merged_json.value("userCustomKey", "") == "custom_value");

    bool has_custom_tool = false;
    for (const auto& d : merged_json["permissions"]["deny"]) {
      if (d == "CustomTool") has_custom_tool = true;
    }
    CHECK(has_custom_tool);
  }

  SUBCASE("Non-JSON overwrite protection and --force") {
    ws.write_file("AGENTS.md", "# Custom Rules\nDo not overwrite me!\n");

    // Without --force: should NOT overwrite
    int rc3 = cli::cmd_init(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc3 == 0);
    CHECK(ws.read_file("AGENTS.md") == "# Custom Rules\nDo not overwrite me!\n");

    // With --force: should overwrite
    std::vector<const char*> force_args = {
        "graphstate", "init", "--host", "claude-code", "--root", root_str.c_str(), "--force"
    };
    int rc4 = cli::cmd_init(static_cast<int>(force_args.size()), const_cast<char**>(force_args.data()));
    CHECK(rc4 == 0);
    CHECK(ws.read_file("AGENTS.md").find("Custom Rules") == std::string::npos);
    CHECK(ws.read_file("AGENTS.md").find("GraphState gate is active") != std::string::npos);
  }
}

TEST_CASE("Init: Google Antigravity host kit") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);

  std::vector<const char*> args = {
      "graphstate", "init", "--host", "antigravity", "--root", root_str.c_str()
  };

  int rc = cli::cmd_init(static_cast<int>(args.size()), const_cast<char**>(args.data()));
  CHECK(rc == 0);

  CHECK(ws.file_exists(".agents/mcp_config.json"));
  CHECK(ws.file_exists(".agents/hooks.json"));
  CHECK(ws.file_exists(".agents/rules/graphstate.md"));
  CHECK(ws.file_exists("AGENTS.md"));

  std::string hooks_text = ws.read_file(".agents/hooks.json");
  CHECK(hooks_text.find("{{GRAPHSTATE_BIN}}") == std::string::npos);
  auto hooks_json = nlohmann::json::parse(hooks_text);
  CHECK(hooks_json.contains("graphstate-gate"));
}

TEST_CASE("Init: Cursor host kit") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);

  std::vector<const char*> args = {
      "graphstate", "init", "--host", "cursor", "--root", root_str.c_str()
  };

  int rc = cli::cmd_init(static_cast<int>(args.size()), const_cast<char**>(args.data()));
  CHECK(rc == 0);

  CHECK(ws.file_exists(".cursor/mcp.json"));
  CHECK(ws.file_exists(".cursor/hooks.json"));
  CHECK(ws.file_exists(".cursor/rules/graphstate.mdc"));
  CHECK(ws.file_exists("AGENTS.md"));

  std::string mcp_text = ws.read_file(".cursor/mcp.json");
  auto mcp_json = nlohmann::json::parse(mcp_text);
  CHECK(mcp_json.contains("mcpServers"));
}

TEST_CASE("Init: Claude Desktop config output") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);

  std::vector<const char*> args = {
      "graphstate", "init", "--host", "claude-desktop", "--root", root_str.c_str()
  };

  int rc = cli::cmd_init(static_cast<int>(args.size()), const_cast<char**>(args.data()));
  CHECK(rc == 0);
}
