#include "cli/cli.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "mcp/server.hpp"
#include "mcp/transport.hpp"
#include "platform/platform.hpp"

namespace gs::cli {

int cmd_serve(int argc, char* argv[]) {
  std::filesystem::path root = std::filesystem::current_path();

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate serve [options]\n\n"
                << "Options:\n"
                << "  --root <path>  Workspace root (default: current directory)\n"
                << "  -h, --help     Show this help message\n";
      return 0;
    }
  }

  try {
    auto config = core::Config::load(root);
    Engine engine(std::move(config));

    // Try bootstrapping DB schema if DB is up
    if (engine.client().ping()) {
      try {
        engine.initialize();
      } catch (const std::exception& e) {
        std::cerr << "Warning: DB initialization failed: " << e.what() << "\n";
      }
    }

    // Startup reconcile
    try {
      engine.indexer().startup_reconcile();
    } catch (const std::exception& e) {
      std::cerr << "Warning: Startup indexing error: " << e.what() << "\n";
    }

    // Enter MCP loop over stdio
    mcp::StdioTransport transport;
    mcp::Server server(engine, transport);
    server.run();

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal serve error: " << e.what() << "\n";
    return 1;
  }
}

} // namespace gs::cli
