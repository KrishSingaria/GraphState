#include "cli/cli.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "platform/platform.hpp"

namespace gs::cli {

int cmd_index(int argc, char* argv[]) {
  bool full = false;
  std::filesystem::path root = std::filesystem::current_path();

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--full") {
      full = true;
    } else if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate index [options]\n\n"
                << "Options:\n"
                << "  --root <path>  Workspace root (default: current directory)\n"
                << "  --full         Drop code subgraph and rebuild from scratch\n"
                << "  -h, --help     Show this help message\n";
      return 0;
    }
  }

  try {
    auto config = core::Config::load(root);
    gs::Engine engine(std::move(config));
    engine.initialize();

    index::IndexReport report;
    if (full) {
      report = engine.indexer().full_rebuild();
    } else {
      report = engine.indexer().startup_reconcile();
    }

    std::cout << "Indexed " << report.parsed << " files ("
              << report.changed << " changed, "
              << report.deleted << " deleted) in "
              << report.ms << " ms\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error during index: " << e.what() << "\n";
    return 1;
  }
}

} // namespace gs::cli
