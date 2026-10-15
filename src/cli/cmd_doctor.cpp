#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <tree_sitter/api.h>
#include "cli/cli.hpp"
#include "core/clock.hpp"
#include "core/error.hpp"
#include "db/graph_client.hpp"
#include "platform/platform.hpp"
#include "host_kits.inc"

extern "C" {
const TSLanguage *tree_sitter_python(void);
const TSLanguage *tree_sitter_typescript(void);
const TSLanguage *tree_sitter_tsx(void);
const TSLanguage *tree_sitter_javascript(void);
const TSLanguage *tree_sitter_go(void);
const TSLanguage *tree_sitter_rust(void);
const TSLanguage *tree_sitter_java(void);
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_json(void);
#if GS_WITH_CPP_GRAMMAR
const TSLanguage *tree_sitter_cpp(void);
#endif
}

namespace gs::cli {

namespace fs = std::filesystem;

int cmd_doctor(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int port = 6390;

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

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--falkor" && i + 1 < argc) {
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

  std::cout << "GraphState System Doctor\n"
            << "========================\n\n";

  int errors = 0;
  int warnings = 0;

  // 1. Docker check
  {
    auto proc = platform::run_capture({"docker", "version", "--format", "{{.Server.Os}}"}, fs::current_path(), 5000);
    if (proc.exit_code == 0) {
      std::string server_os = proc.out;
      while (!server_os.empty() && (server_os.back() == '\n' || server_os.back() == '\r' || server_os.back() == ' ')) {
        server_os.pop_back();
      }
      if (server_os == "linux") {
        std::cout << "  [OK] Docker reachable (Server OS: linux)\n";
      } else {
        std::cout << "  [FAIL] Docker Server OS is '" << server_os << "'. FalkorDB requires Linux containers!\n";
        errors++;
      }
    } else {
      std::cout << "  [WARN] Docker CLI not reachable or returned error (skipping docker daemon check)\n";
      warnings++;
    }
  }

  // 2. FalkorDB connection & PING
  std::unique_ptr<db::GraphClient> client;
  bool db_connected = false;
  try {
    core::Stopwatch sw;
    client = std::make_unique<db::GraphClient>(host, port, "", 3000, 5000);
    if (client->ping()) {
      double latency = sw.elapsed_ms();
      std::cout << "  [OK] FalkorDB reachable at " << host << ":" << port
                << " (PING: " << std::fixed << std::setprecision(2) << latency << " ms)\n";
      db_connected = true;
    } else {
      std::cout << "  [FAIL] FalkorDB PING returned false at " << host << ":" << port << "\n";
      errors++;
    }
  } catch (const gs::Error& e) {
    std::cout << "  [FAIL] FalkorDB connection failed at " << host << ":" << port << ": " << e.what() << "\n";
    errors++;
  } catch (const std::exception& e) {
    std::cout << "  [FAIL] FalkorDB connection exception: " << e.what() << "\n";
    errors++;
  }

  // 3. FalkorDB module & version
  if (db_connected && client) {
    try {
      auto modules = client->raw({"MODULE", "LIST"});
      bool found_graph = false;
      int64_t ver = 0;
      int cache_size = 0;

      if (modules.is_array()) {
        auto inspect_module_fields = [&](const nlohmann::json& fields) {
          if (!fields.is_array()) return;
          for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].is_string() && fields[i].get<std::string>() == "name" && i + 1 < fields.size()) {
              std::string mod_name = fields[i + 1].get<std::string>();
              if (mod_name == "graph" || mod_name == "falkordb") {
                found_graph = true;
                for (size_t j = 0; j < fields.size(); ++j) {
                  if (fields[j].is_string() && fields[j].get<std::string>() == "ver" && j + 1 < fields.size()) {
                    if (fields[j + 1].is_number()) {
                      ver = fields[j + 1].get<int64_t>();
                    }
                  }
                  if (fields[j].is_string() && fields[j].get<std::string>() == "args" && j + 1 < fields.size()) {
                    const auto& args = fields[j + 1];
                    if (args.is_array()) {
                      for (size_t k = 0; k < args.size(); ++k) {
                        if (args[k].is_string() && args[k].get<std::string>() == "CACHE_SIZE" && k + 1 < args.size()) {
                          if (args[k + 1].is_string()) {
                            cache_size = std::atoi(args[k + 1].get<std::string>().c_str());
                          } else if (args[k + 1].is_number()) {
                            cache_size = args[k + 1].get<int>();
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        };

        for (const auto& item : modules) {
          if (item.is_array()) {
            inspect_module_fields(item);
          }
        }
        if (!found_graph) {
          inspect_module_fields(modules);
        }
      }

      if (found_graph) {
        int v_major = static_cast<int>(ver / 10000);
        int v_minor = static_cast<int>((ver % 10000) / 100);
        int v_patch = static_cast<int>(ver % 100);
        std::cout << "  [OK] FalkorDB module verified (version " << v_major << "." << v_minor << "." << v_patch << ")\n";
      } else {
        std::cout << "  [FAIL] FalkorDB graph module not found in MODULE LIST\n";
        errors++;
      }

      // 4. Persistence (AOF)
      auto aof_cfg = client->raw({"CONFIG", "GET", "appendonly"});
      bool aof_ok = false;
      if (aof_cfg.is_array() && aof_cfg.size() >= 2) {
        if (aof_cfg[1].is_string() && aof_cfg[1].get<std::string>() == "yes") {
          aof_ok = true;
        }
      }
      if (aof_ok) {
        std::cout << "  [OK] FalkorDB AOF persistence enabled (appendonly: yes)\n";
      } else {
        std::cout << "  [WARN] FalkorDB AOF persistence not enabled (CONFIG GET appendonly is not 'yes')\n";
        warnings++;
      }

      // 5. Plan cache size
      if (cache_size >= 64) {
        std::cout << "  [OK] FalkorDB query plan cache size: " << cache_size << " (>= 64)\n";
      } else if (cache_size > 0) {
        std::cout << "  [WARN] FalkorDB query plan cache size: " << cache_size << " (< 64, may cause LRU thrashing)\n";
        warnings++;
      } else {
        std::cout << "  [OK] FalkorDB query plan cache active\n";
      }
    } catch (const std::exception& e) {
      std::cout << "  [WARN] Failed to query FalkorDB configs: " << e.what() << "\n";
      warnings++;
    }
  }

  // 6. Tree-sitter grammars & ABI
  {
    struct GrammarCheck {
      const char* name;
      const TSLanguage* (*fn)(void);
    };

    std::vector<GrammarCheck> grammars = {
        {"python", tree_sitter_python},
        {"typescript", tree_sitter_typescript},
        {"tsx", tree_sitter_tsx},
        {"javascript", tree_sitter_javascript},
        {"go", tree_sitter_go},
        {"rust", tree_sitter_rust},
        {"java", tree_sitter_java},
        {"c", tree_sitter_c},
        {"json", tree_sitter_json},
#if GS_WITH_CPP_GRAMMAR
        {"cpp", tree_sitter_cpp},
#endif
    };

    bool grammars_ok = true;
    uint32_t abi_sample = 0;
    for (const auto& g : grammars) {
      const TSLanguage* lang = g.fn();
      if (!lang) {
        std::cout << "  [FAIL] Failed to load grammar: " << g.name << "\n";
        grammars_ok = false;
        errors++;
        continue;
      }
      uint32_t abi = ts_language_abi_version(lang);
      abi_sample = abi;
      if (abi < 13 || abi > 15) {
        std::cout << "  [FAIL] Grammar " << g.name << " ABI version " << abi << " outside [13, 15]\n";
        grammars_ok = false;
        errors++;
      }
    }
    if (grammars_ok) {
      std::cout << "  [OK] Tree-sitter core + all " << grammars.size() << " grammars loaded (ABI v" << abi_sample << ")\n";
    }
  }

  // 7. Workspace writable check
  {
    fs::path test_probe = fs::current_path() / ".graphstate_doctor_probe";
    try {
      platform::write_file_durable(test_probe, "probe");
      platform::remove_file(test_probe);
      std::cout << "  [OK] Workspace directory is writable (" << platform::to_utf8(fs::current_path()) << ")\n";
    } catch (const std::exception& e) {
      std::cout << "  [FAIL] Workspace directory is not writable: " << e.what() << "\n";
      errors++;
    }
  }

  // 8. Host-kit files embedded
  {
    size_t host_kits_count = std::size(embedded::HOST_KITS);
    if (host_kits_count > 0) {
      std::cout << "  [OK] Host-kit templates embedded (" << host_kits_count << " files available)\n";
    } else {
      std::cout << "  [FAIL] No host-kit templates found embedded\n";
      errors++;
    }
  }

  // 9. Windows Defender exclusion tip
  if (platform::os_name() == "windows") {
    std::cout << "\nTip: To avoid Windows Defender file-locking slowdowns during atomic writes:\n"
              << "     Add-MpPreference -ExclusionPath \"" << platform::to_utf8(fs::current_path()) << "\"\n";
  }

  std::cout << "\nDoctor Summary: " << (errors == 0 ? "ALL CHECKS GREEN" : "ISSUES DETECTED")
            << " (" << errors << " errors, " << warnings << " warnings)\n";

  return (errors == 0) ? 0 : 1;
}

} // namespace gs::cli
