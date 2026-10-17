#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "gate/patch.hpp"
#include "lang/facts.hpp"
#include "lang/language.hpp"
#include "lang/ts_raii.hpp"

namespace gs::gate {

struct RuleConfig {
  std::string id;
  std::string severity = "warn"; // "error", "warn", "off"
  int max = -1;                  // for max-* rules
  int min_lines = -1;            // for py-public-docstring
  std::vector<std::string> exclude_paths;
};

struct CustomRule {
  std::string id;
  lang::Language language = lang::Language::Unknown;
  Severity severity = Severity::Error;
  std::vector<std::string> paths; // glob patterns
  std::string query_str;
  std::string unless_query_str;
  std::string message;
  std::string fix_hint;

  // Compiled queries
  lang::QueryPtr query;
  lang::QueryPtr unless_query;
};

class RulesEngine {
public:
  RulesEngine();
  explicit RulesEngine(const std::filesystem::path& root);
  ~RulesEngine();

  // Load or reload .graphstate/rules.json if present
  void load_config(const std::filesystem::path& root);

  // Run all active rules on a file's post-state
  // modified_ranges limits AST node inspections to touched symbol ranges
  std::vector<Violation> check(const std::string& file,
                               lang::Language lang,
                               std::string_view post_source,
                               const lang::FileFacts& post_facts,
                               const std::vector<lang::ByteRange>& modified_ranges) const;

  const std::vector<std::string>& rule_errors() const { return rule_errors_; }
  const std::map<std::string, RuleConfig>& rules() const { return rules_; }

private:
  void init_defaults();
  bool is_path_excluded(const std::string& file, const std::vector<std::string>& patterns) const;

  std::map<std::string, RuleConfig> rules_;
  std::vector<CustomRule> custom_rules_;
  std::vector<std::string> rule_errors_;
};

} // namespace gs::gate
