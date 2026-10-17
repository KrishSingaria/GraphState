#include "cli/cli.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include "platform/platform.hpp"

#ifndef GS_VERSION
#define GS_VERSION "0.1.0"
#endif

#ifndef GS_TREESITTER_VERSION
#define GS_TREESITTER_VERSION "0.27.1"
#endif

#ifndef GS_HIREDIS_VERSION
#define GS_HIREDIS_VERSION "1.4.1"
#endif

namespace gs::cli {

int run(int argc, char* argv[]) {
  if (argc <= 1) {
    std::cout << "Usage: graphstate <command> [options]\n\n"
              << "Commands:\n"
              << "  serve       MCP server over stdio\n"
              << "  index       Index/reconcile workspace\n"
              << "  status      Show graph and session status\n"
              << "  doctor      Check system environment and DB\n"
              << "  query       Run Cypher query against project graph\n"
              << "  bench       Run latency benchmark\n"
              << "  forget      Delete memories or pitfalls\n"
              << "  init        Setup host configuration\n"
              << "  hook        Execute MCP host hooks\n"
              << "  changeset   Manage changesets\n"
              << "  audit       Consistency check on workspace\n"
              << "  version     Show version information\n";
    return 0;
  }

  std::string_view cmd = argv[1];

  if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    std::string arch = "x64";
#if defined(__aarch64__) || defined(_M_ARM64)
    arch = "arm64";
#endif
    std::string os_arch = platform::os_name() + "-" + arch;

    std::cout << "graphstate " << GS_VERSION
              << " (tree-sitter " << GS_TREESITTER_VERSION
              << ", hiredis " << GS_HIREDIS_VERSION
              << ", " << os_arch << ")\n";
    return 0;
  }

  if (cmd == "serve") {
    return cmd_serve(argc, argv);
  }

  if (cmd == "doctor") {
    return cmd_doctor(argc, argv);
  }

  if (cmd == "query") {
    return cmd_query(argc, argv);
  }

  if (cmd == "debug-facts") {
    return cmd_debug_facts(argc, argv);
  }

  if (cmd == "index") {
    return cmd_index(argc, argv);
  }

  if (cmd == "status") {
    return cmd_status(argc, argv);
  }

  if (cmd == "changeset") {
    return cmd_changeset(argc, argv);
  }

  if (cmd == "init") {
    return cmd_init(argc, argv);
  }

  if (cmd == "hook") {
    return cmd_hook(argc, argv);
  }

  std::cerr << "Unknown command or not yet implemented: " << cmd << "\n";
  return 1;
}

} // namespace gs::cli
