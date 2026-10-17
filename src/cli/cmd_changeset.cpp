#include "cli/cli.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include <nlohmann/json.hpp>

#include "core/config.hpp"
#include "core/engine.hpp"
#include "gate/changesets.hpp"
#include "mcp/tool_registry.hpp"
#include "memory/sessions.hpp"
#include "platform/platform.hpp"

namespace gs::cli {

int cmd_changeset(int argc, char* argv[]) {
  if (argc <= 2) {
    std::cout << "Usage: graphstate changeset <subcommand> [options]\n\n"
              << "Subcommands:\n"
              << "  status           Show current open changeset and obligations\n"
              << "  close, finalize  Finalize and close open changeset (use --force to override open obligations)\n"
              << "  abandon          Abandon open changeset and revert files (use --force to override conflicts)\n\n"
              << "Options:\n"
              << "  --root <path>    Workspace root (default: current directory)\n"
              << "  --force          Force close or abandon\n"
              << "  --json           Output as JSON\n"
              << "  -h, --help       Show this help message\n";
    return 0;
  }

  std::string_view subcmd = argv[2];
  if (subcmd == "-h" || subcmd == "--help") {
    std::cout << "Usage: graphstate changeset <subcommand> [options]\n";
    return 0;
  }

  bool force = false;
  bool as_json = false;
  std::filesystem::path root = std::filesystem::current_path();

  for (int i = 3; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--force") {
      force = true;
    } else if (arg == "--json") {
      as_json = true;
    } else if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    }
  }

  try {
    auto config = core::Config::load(root);
    Engine engine(std::move(config));
    engine.initialize();

    memory::SessionCtx session{engine.sessions().ctx().session_id, "", "cli"};

    if (subcmd == "status") {
      auto cs = engine.changesets().current();
      if (!cs.has_value() || cs->status != "OPEN") {
        if (as_json) {
          std::cout << nlohmann::json{{"status", "NONE"}}.dump(2) << "\n";
        } else {
          std::cout << "No changeset is currently open.\n";
        }
        return 0;
      }

      if (as_json) {
        std::cout << gate::to_json(*cs).dump(2) << "\n";
      } else {
        std::cout << "Changeset " << cs->id << " (OPEN)\n"
                  << "  Intent: " << cs->intent << "\n"
                  << "  Files:  " << cs->files.size() << " touched\n"
                  << "  Open obligations: " << cs->obligations.size() << "\n";
        for (const auto& ob : cs->obligations) {
          std::string loc = ob.violation.location.has_value()
                                ? (ob.violation.location->file + ":" + std::to_string(ob.violation.location->line))
                                : "";
          std::cout << "    - [" << ob.violation.code << "] " << loc << " — " << ob.violation.message << "\n";
        }
      }
      return 0;
    }

    if (subcmd == "close" || subcmd == "finalize") {
      try {
        auto cs = engine.changesets().finalize(session, force);
        if (cs.status == "OPEN") {
          if (as_json) {
            std::cout << gate::to_json(cs).dump(2) << "\n";
          } else {
            std::cerr << "Changeset " << cs.id << " has " << cs.obligations.size()
                      << " remaining obligations. Use --force to close anyway.\n";
          }
          return 1;
        }

        if (as_json) {
          std::cout << gate::to_json(cs).dump(2) << "\n";
        } else {
          std::cout << "Changeset " << cs.id << " closed.\n";
        }
        return 0;
      } catch (const mcp::ToolError& err) {
        std::cerr << "Error: " << err.what() << "\n";
        return 1;
      }
    }

    if (subcmd == "abandon") {
      try {
        auto cs = engine.changesets().abandon(session, force);
        if (!cs.conflicts.empty()) {
          if (as_json) {
            std::cout << gate::to_json(cs).dump(2) << "\n";
          } else {
            std::cerr << "Conflict: files were modified outside the changeset. Use --force to abandon anyway.\n";
            for (const auto& c : cs.conflicts) {
              std::cerr << "  - " << c << "\n";
            }
          }
          return 1;
        }

        if (as_json) {
          std::cout << gate::to_json(cs).dump(2) << "\n";
        } else {
          std::cout << "Changeset " << cs.id << " abandoned and files restored.\n";
        }
        return 0;
      } catch (const mcp::ToolError& err) {
        std::cerr << "Error: " << err.what() << "\n";
        return 1;
      }
    }

    std::cerr << "Unknown changeset subcommand: " << subcmd << "\n";
    return 1;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
}

} // namespace gs::cli
