#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include "cli/cli.hpp"
#include "core/config.hpp"
#include "gate/audit.hpp"
#include "platform/platform.hpp"

namespace gs::cli {

int cmd_audit(int argc, char* argv[]) {
  std::filesystem::path root = ".";
  std::optional<std::filesystem::path> base;
  bool output_json = false;

  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--root" && i + 1 < argc) {
      root = platform::from_utf8(argv[++i]);
    } else if (arg == "--base" && i + 1 < argc) {
      base = platform::from_utf8(argv[++i]);
    } else if (arg == "--json") {
      output_json = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: graphstate audit [options]\n\n"
                << "Whole-workspace consistency check requiring no database connection.\n\n"
                << "Options:\n"
                << "  --root <dir>      Workspace root directory to audit (default: .)\n"
                << "  --base <dir>      Optional pristine base directory (reports only introduced findings)\n"
                << "  --json            Output structured JSON report to stdout\n"
                << "  --help, -h        Show this help message\n";
      return 0;
    }
  }

  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    if (output_json) {
      nlohmann::json err_j;
      err_j["findings"] = nlohmann::json::array();
      err_j["counts"] = nlohmann::json::object();
      err_j["files"] = 0;
      err_j["error"] = "Workspace root does not exist: " + platform::to_utf8(root);
      std::cout << err_j.dump(2) << "\n";
    } else {
      std::cerr << "Error: workspace root does not exist: " << platform::to_utf8(root) << "\n";
    }
    return 0; // Measurement tool exits 0
  }

  if (base.has_value() && !std::filesystem::exists(*base, ec)) {
    if (output_json) {
      nlohmann::json err_j;
      err_j["findings"] = nlohmann::json::array();
      err_j["counts"] = nlohmann::json::object();
      err_j["files"] = 0;
      err_j["error"] = "Base workspace does not exist: " + platform::to_utf8(*base);
      std::cout << err_j.dump(2) << "\n";
    } else {
      std::cerr << "Error: base workspace does not exist: " << platform::to_utf8(*base) << "\n";
    }
    return 0;
  }

  core::Config config;
  config.root = root;

  auto report = gate::audit_workspace(config, root, base);

  if (output_json) {
    std::cout << report.to_json().dump(2) << "\n";
    return 0;
  }

  // Human-readable output
  if (report.findings.empty()) {
    std::cout << "Audit clean: 0 findings across " << report.files << " files";
    if (base.has_value()) {
      std::cout << " (diffed against base: " << platform::to_utf8(*base) << ")";
    }
    std::cout << ".\n";
    return 0;
  }

  std::cout << "Audit found " << report.findings.size() << " issue(s) across "
            << report.files << " files";
  if (base.has_value()) {
    std::cout << " (introduced vs base: " << platform::to_utf8(*base) << ")";
  }
  std::cout << ":\n";

  if (!report.counts.empty()) {
    std::cout << "Summary: ";
    bool first = true;
    for (const auto& [code, count] : report.counts) {
      if (!first) std::cout << ", ";
      std::cout << code << ": " << count;
      first = false;
    }
    std::cout << "\n\n";
  }

  for (const auto& f : report.findings) {
    std::cout << "  " << f.file << ":" << f.line << ":" << f.col
              << "  [" << f.code << "]  " << f.message << "\n";
  }

  return 0;
}

} // namespace gs::cli
