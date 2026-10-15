#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "cli/cli.hpp"
#include "core/error.hpp"
#include "db/graph_client.hpp"

namespace gs::cli {

int cmd_query(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: graphstate query \"<cypher>\" [--ro] [--graph <name>] [--falkor <host:port>]\n";
    return 1;
  }

  std::string cypher = argv[2];
  bool read_only = false;
  std::string graph = "gs:default";
  std::string host = "127.0.0.1";
  int port = 6390;

  // Check env vars for falkor host/port
  if (const char* env_falkor = std::getenv("GS_FALKOR")) {
    std::string s(env_falkor);
    size_t col = s.find(':');
    if (col != std::string::npos) {
      host = s.substr(0, col);
      port = std::atoi(s.substr(col + 1).c_str());
    } else {
      host = s;
    }
  }

  for (int i = 3; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--ro") {
      read_only = true;
    } else if (arg == "--graph" && i + 1 < argc) {
      graph = argv[++i];
    } else if (arg == "--falkor" && i + 1 < argc) {
      std::string s = argv[++i];
      size_t col = s.find(':');
      if (col != std::string::npos) {
        host = s.substr(0, col);
        port = std::atoi(s.substr(col + 1).c_str());
      } else {
        host = s;
      }
    }
  }

  try {
    db::GraphClient client(host, port);
    db::Query q{"cli_query", cypher, nlohmann::json::object(), read_only};

    if (read_only || cypher.find("RETURN") != std::string::npos || cypher.find("return") != std::string::npos) {
      auto rows = client.rows(graph, q);
      std::cout << nlohmann::json(rows).dump(2) << "\n";
    } else {
      auto stats = client.exec(graph, q);
      std::cout << "Nodes created: " << stats.nodes_created << "\n"
                << "Nodes deleted: " << stats.nodes_deleted << "\n"
                << "Relationships created: " << stats.rels_created << "\n"
                << "Relationships deleted: " << stats.rels_deleted << "\n"
                << "Properties set: " << stats.props_set << "\n"
                << "Cached execution: " << (stats.cached ? 1 : 0) << "\n"
                << "Query internal execution time: " << stats.exec_ms << " milliseconds\n";
    }
    return 0;
  } catch (const gs::Error& e) {
    std::cerr << "Error [" << error_code_name(e.code) << "]: " << e.what() << "\n";
    return (e.code == ErrorCode::DB_UNAVAILABLE) ? 2 : 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

} // namespace gs::cli
