#include "cli/cli.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include <nlohmann/json.hpp>
#include "core/config.hpp"
#include "db/graph_client.hpp"
#include "db/queries.hpp"
#include "platform/platform.hpp"

namespace gs::cli {

int cmd_status(int argc, char* argv[]) {
  bool as_json = false;
  std::filesystem::path root = std::filesystem::current_path();

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--json") {
      as_json = true;
    } else if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate status [options]\n\n"
                << "Options:\n"
                << "  --root <path>  Workspace root (default: current directory)\n"
                << "  --json         Output status as JSON\n"
                << "  -h, --help     Show this help message\n";
      return 0;
    }
  }

  try {
    auto config = core::Config::load(root);
    db::GraphClient client(
        config.falkordb.host,
        config.falkordb.port,
        config.falkordb.password,
        config.falkordb.connect_timeout_ms,
        config.falkordb.query_timeout_ms);

    if (!client.ping()) {
      std::cerr << "Error: FalkorDB is unavailable on "
                << config.falkordb.host << ":" << config.falkordb.port << "\n";
      return 1;
    }

    std::string project_graph = config.project_graph();
    auto count_rows = client.rows(project_graph, {"counts", db::Q_COUNTS, nlohmann::json::object(), true});
    auto meta_row = client.one(project_graph, {"meta_get", db::Q_META_GET, nlohmann::json::object(), true});

    nlohmann::json counts = nlohmann::json::object();
    for (const auto& r : count_rows) {
      if (r.contains("label") && r.contains("n")) {
        counts[r["label"].get<std::string>()] = r["n"].get<int64_t>();
      }
    }

    int64_t last_indexed_at = 0;
    int schema_version = 0;
    if (!meta_row.is_null()) {
      if (meta_row.contains("last_indexed_at") && !meta_row["last_indexed_at"].is_null()) {
        last_indexed_at = meta_row["last_indexed_at"].get<int64_t>();
      }
      if (meta_row.contains("schema_version") && !meta_row["schema_version"].is_null()) {
        schema_version = meta_row["schema_version"].get<int>();
      }
    }

    if (as_json) {
      nlohmann::json out = {
        {"root", platform::to_utf8(config.root)},
        {"workspace_key", config.workspace_key()},
        {"graph", project_graph},
        {"schema_version", schema_version},
        {"last_indexed_at", last_indexed_at},
        {"counts", counts}
      };
      std::cout << out.dump(2) << "\n";
    } else {
      std::cout << "Workspace:      " << platform::to_utf8(config.root) << "\n"
                << "Workspace Key:  " << config.workspace_key() << "\n"
                << "Project Graph:  " << project_graph << "\n"
                << "Schema Version: " << schema_version << "\n"
                << "Last Indexed:   " << last_indexed_at << "\n\n"
                << "Node Counts:\n";
      if (counts.empty()) {
        std::cout << "  (graph is empty)\n";
      } else {
        for (auto it = counts.begin(); it != counts.end(); ++it) {
          std::cout << "  " << it.key() << ": " << it.value() << "\n";
        }
      }
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error fetching status: " << e.what() << "\n";
    return 1;
  }
}

} // namespace gs::cli
