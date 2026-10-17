#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "cli/cli.hpp"
#include "core/config.hpp"
#include "db/graph_client.hpp"
#include "platform/platform.hpp"

namespace gs::cli {

int cmd_forget(int argc, char* argv[]) {
  std::filesystem::path root = ".";
  std::string id_to_delete;
  bool all_memory = false;
  bool pitfalls = false;
  bool target_user = false;
  bool assume_yes = false;

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--id" && i + 1 < argc) {
      id_to_delete = argv[++i];
    } else if (arg == "--all-memory") {
      all_memory = true;
    } else if (arg == "--pitfalls") {
      pitfalls = true;
    } else if (arg == "--user") {
      target_user = true;
    } else if (arg == "--yes" || arg == "-y") {
      assume_yes = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate forget [options]\n\n"
                << "Deletes memories or pitfalls from FalkorDB.\n\n"
                << "Options:\n"
                << "  --root <dir>       Workspace root (default: .)\n"
                << "  --id <id>          Delete a specific Memory or Pitfall by ID\n"
                << "  --all-memory       Delete all Memory nodes in the graph\n"
                << "  --pitfalls         Delete all Pitfall nodes in the graph\n"
                << "  --user             Operate on shared user graph (gs:__user__)\n"
                << "  --yes, -y          Confirm deletion without prompting\n"
                << "  --help, -h         Show this help message\n";
      return 0;
    }
  }

  if (id_to_delete.empty() && !all_memory && !pitfalls) {
    std::cerr << "Error: Must specify --id <id>, --all-memory, or --pitfalls.\n";
    return 1;
  }

  core::Config config;
  config.root = root;

  db::GraphClient client(config.falkordb.host, config.falkordb.port, "", config.falkordb.connect_timeout_ms);
  if (!client.ping()) {
    std::cerr << "Error: Cannot connect to FalkorDB at " << config.falkordb.host << ":" << config.falkordb.port << "\n";
    return 1;
  }

  std::string graph_name = target_user ? config.user_graph() : config.project_graph();

  if (!assume_yes) {
    std::cout << "Are you sure you want to delete from graph '" << graph_name << "'? (y/N): ";
    std::string ans;
    if (!(std::cin >> ans) || (ans != "y" && ans != "Y")) {
      std::cout << "Aborted.\n";
      return 0;
    }
  }

  try {
    if (!id_to_delete.empty()) {
      client.exec(graph_name, db::Query{
          .name = "forget_by_id",
          .cypher = "MATCH (m) WHERE (m:Memory OR m:Pitfall) AND m.id = $id DETACH DELETE m",
          .params = {{"id", id_to_delete}},
          .read_only = false
      });
      std::cout << "Deleted node with id '" << id_to_delete << "'.\n";
    }

    if (all_memory) {
      client.exec(graph_name, db::Query{
          .name = "forget_all_memory",
          .cypher = "MATCH (m:Memory) DETACH DELETE m",
          .params = nlohmann::json::object(),
          .read_only = false
      });
      std::cout << "Deleted all Memory nodes from " << graph_name << ".\n";
    }

    if (pitfalls) {
      client.exec(graph_name, db::Query{
          .name = "forget_pitfalls",
          .cypher = "MATCH (p:Pitfall) DETACH DELETE p",
          .params = nlohmann::json::object(),
          .read_only = false
      });
      std::cout << "Deleted all Pitfall nodes from " << graph_name << ".\n";
    }

    std::cout << "Forget operation completed successfully.\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error deleting memories: " << e.what() << "\n";
    return 1;
  }
}

} // namespace gs::cli
