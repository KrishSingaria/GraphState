#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "core/config.hpp"
#include "index/fact_store.hpp"

namespace gs::gate {

struct AuditFinding {
  std::string code;     // SYNTAX_ERROR, CALL_INCOMPATIBLE, MISSING_SYMBOL, BROKEN_IMPORT, MISSING_MODULE
  std::string file;     // relative path (forward slashes)
  int line{0};
  int col{0};
  std::string message;
  std::string symbol;   // caller or referencing symbol
  std::string callee;   // target symbol or module
  nlohmann::json evidence;

  std::string diff_key() const {
    return code + "|" + file + "|" + symbol + "|" + callee;
  }
};

struct AuditReport {
  std::vector<AuditFinding> findings;
  std::map<std::string, int> counts;
  size_t files{0};

  nlohmann::json to_json() const;
};

// Audits an in-memory FactStore and appends syntax errors.
AuditReport audit_fact_store(
    const core::Config& config,
    const index::FactStore& store,
    const std::vector<AuditFinding>& syntax_errors = {});

// Filters current_report to return only introduced findings relative to base_report.
AuditReport diff_audit_reports(
    const AuditReport& base_report,
    const AuditReport& current_report);

// Whole-workspace consistency check without database connection.
// If base is provided, reports only findings introduced relative to base.
AuditReport audit_workspace(
    const core::Config& config,
    const std::filesystem::path& root,
    const std::optional<std::filesystem::path>& base = std::nullopt);

} // namespace gs::gate
