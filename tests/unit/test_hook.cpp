#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <iostream>
#include <sstream>
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
    root = std::filesystem::temp_directory_path() / ("gs_test_hook_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

struct StreamCapture {
  std::streambuf* cin_buf;
  std::streambuf* cout_buf;
  std::streambuf* cerr_buf;
  std::stringstream in_stream;
  std::stringstream out_stream;
  std::stringstream err_stream;

  explicit StreamCapture(const std::string& input) {
    in_stream << input;
    cin_buf = std::cin.rdbuf(in_stream.rdbuf());
    cout_buf = std::cout.rdbuf(out_stream.rdbuf());
    cerr_buf = std::cerr.rdbuf(err_stream.rdbuf());
  }

  ~StreamCapture() {
    std::cin.rdbuf(cin_buf);
    std::cout.rdbuf(cout_buf);
    std::cerr.rdbuf(cerr_buf);
  }
};

} // namespace

TEST_CASE("Hook: Claude Code deny-edit enforcement") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);
  std::filesystem::path target_file = ws.root / "src" / "index.py";
  std::string target_str = platform::to_utf8(target_file);

  SUBCASE("Block edit inside workspace root") {
    nlohmann::json hook_input = {
        {"tool_name", "Edit"},
        {"tool_input", {
            {"file_path", target_str}
        }}
    };

    StreamCapture capture(hook_input.dump());
    std::vector<const char*> args = {
        "graphstate", "hook", "deny-edit", "--host", "claude-code", "--root", root_str.c_str()
    };

    int rc = cli::cmd_hook(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc == 2);
    CHECK(capture.err_stream.str().find("direct edits are disabled in this repository") != std::string::npos);
    CHECK(capture.err_stream.str().find("mcp__graphstate__propose_patch") != std::string::npos);
  }

  SUBCASE("Allow edit outside workspace root") {
    std::filesystem::path outside_file = std::filesystem::temp_directory_path() / "other_outside_dir" / "out.py";
    std::string outside_str = platform::to_utf8(outside_file);

    nlohmann::json hook_input = {
        {"tool_name", "Edit"},
        {"tool_input", {
            {"file_path", outside_str}
        }}
    };

    StreamCapture capture(hook_input.dump());
    std::vector<const char*> args = {
        "graphstate", "hook", "deny-edit", "--host", "claude-code", "--root", root_str.c_str()
    };

    int rc = cli::cmd_hook(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc == 0);
  }
}

TEST_CASE("Hook: Google Antigravity deny-edit enforcement") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);
  std::filesystem::path target_file = ws.root / "app.ts";
  std::string target_str = platform::to_utf8(target_file);

  SUBCASE("Deny edit inside workspace root") {
    nlohmann::json hook_input = {
        {"tool_input", {
            {"TargetFile", target_str}
        }}
    };

    StreamCapture capture(hook_input.dump());
    std::vector<const char*> args = {
        "graphstate", "hook", "deny-edit", "--host", "antigravity", "--root", root_str.c_str()
    };

    int rc = cli::cmd_hook(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc == 0);

    auto out_json = nlohmann::json::parse(capture.out_stream.str());
    CHECK(out_json.value("decision", "") == "deny");
    CHECK(out_json.value("reason", "").find("direct edits are disabled") != std::string::npos);
  }

  SUBCASE("Allow edit outside workspace root") {
    std::filesystem::path outside_file = std::filesystem::temp_directory_path() / "outside_dir_antigravity" / "external.ts";
    std::string outside_str = platform::to_utf8(outside_file);

    nlohmann::json hook_input = {
        {"tool_input", {
            {"TargetFile", outside_str}
        }}
    };

    StreamCapture capture(hook_input.dump());
    std::vector<const char*> args = {
        "graphstate", "hook", "deny-edit", "--host", "antigravity", "--root", root_str.c_str()
    };

    int rc = cli::cmd_hook(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc == 0);
    CHECK(capture.out_stream.str().empty());
  }
}

TEST_CASE("Hook: Cursor deny-edit enforcement") {
  TempWorkspace ws;
  std::string root_str = platform::to_utf8(ws.root);
  std::filesystem::path target_file = ws.root / "index.js";
  std::string target_str = platform::to_utf8(target_file);

  SUBCASE("Deny edit inside workspace root") {
    nlohmann::json hook_input = {
        {"path", target_str}
    };

    StreamCapture capture(hook_input.dump());
    std::vector<const char*> args = {
        "graphstate", "hook", "deny-edit", "--host", "cursor", "--root", root_str.c_str()
    };

    int rc = cli::cmd_hook(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc == 0);

    auto out_json = nlohmann::json::parse(capture.out_stream.str());
    CHECK(out_json.value("permission", "") == "deny");
    CHECK(out_json.contains("agent_message"));
    CHECK(out_json.contains("user_message"));
  }

  SUBCASE("Allow edit outside workspace root") {
    std::filesystem::path outside_file = std::filesystem::temp_directory_path() / "outside_cursor" / "out.js";
    std::string outside_str = platform::to_utf8(outside_file);

    nlohmann::json hook_input = {
        {"path", outside_str}
    };

    StreamCapture capture(hook_input.dump());
    std::vector<const char*> args = {
        "graphstate", "hook", "deny-edit", "--host", "cursor", "--root", root_str.c_str()
    };

    int rc = cli::cmd_hook(static_cast<int>(args.size()), const_cast<char**>(args.data()));
    CHECK(rc == 0);
    CHECK(capture.out_stream.str().empty());
  }
}
