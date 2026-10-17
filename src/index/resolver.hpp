#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lang/facts.hpp"

namespace gs::core {
struct Config;
}

namespace gs::index {

class FactStore;

struct ResolvedCall {
  std::string caller_id;
  std::string target_id;
  int line{0};
  int col{0};
  int pos_args{0};
  std::vector<std::string> kw_names;
  bool has_star{false};
  bool has_dstar{false};
  std::string confidence; // "exact" | "heuristic"
  std::string call_kind;  // "plain" | "instance" | "classmethod" | "constructor" | "unbound"
  std::string call_text;
  const lang::CallFact* call{nullptr};

  bool operator==(const ResolvedCall& o) const = default;
};

struct ResolvedImport {
  std::string src_file;
  std::string target_file;
  std::vector<std::string> names;
  bool whole_module{false};
  std::string alias;
  uint32_t line{0};

  bool operator==(const ResolvedImport& o) const = default;
};

struct ResolvedInherit {
  std::string cls;
  std::string base;
  std::string confidence{"exact"};

  bool operator==(const ResolvedInherit& o) const = default;
};

class Resolver {
public:
  Resolver();
  explicit Resolver(std::vector<std::string> python_source_roots);
  explicit Resolver(const core::Config& config);

  void set_python_source_roots(std::vector<std::string> roots);
  const std::vector<std::string>& python_source_roots() const;

  // Module resolution
  std::string resolve_python_module(
      const FactStore& store,
      std::string_view from_file,
      std::string_view import_source) const;

  std::string resolve_ts_module(
      const FactStore& store,
      std::string_view from_file,
      std::string_view import_source) const;

  // Import resolution
  std::vector<ResolvedImport> resolve_imports(
      const FactStore& store,
      std::string_view rel_path) const;

  std::vector<ResolvedImport> resolve_imports_for_file(
      const FactStore& store,
      const lang::FileFacts& facts) const;

  // Inheritance resolution
  std::vector<ResolvedInherit> resolve_inherits(
      const FactStore& store,
      std::string_view rel_path) const;

  std::vector<ResolvedInherit> resolve_inherits_for_file(
      const FactStore& store,
      const lang::FileFacts& facts) const;

  const lang::SymbolFact* find_class_symbol(
      const FactStore& store,
      const lang::FileFacts& facts,
      std::string_view class_name) const;

  // Call resolution
  std::vector<ResolvedCall> resolve_file(
      const FactStore& store,
      std::string_view rel_path) const;

  std::vector<ResolvedCall> resolve_calls(
      const FactStore& store,
      const lang::FileFacts& facts) const;

  // Reverse dependencies
  std::vector<std::string> dependents_of(std::string_view file) const;
  void update_dependencies(
      std::string_view file,
      const std::vector<lang::ImportFact>& imports,
      const FactStore& store);
  void update_dependencies(
      std::string_view file,
      const std::vector<lang::ImportFact>& imports);
  void remove_file(std::string_view file);
  void rebuild_indices(const FactStore& store);

  // Heuristic inspection
  bool is_common_name(std::string_view name) const;

  // Import & class hierarchy inspection
  struct ImportBinding {
    std::string target_file;
    std::string orig_name;
    bool is_whole_module{false};
  };

  std::unordered_map<std::string, ImportBinding> build_import_map(
      const FactStore& store,
      const lang::FileFacts& facts) const;

  const lang::SymbolFact* find_method_in_class_hierarchy(
      const FactStore& store,
      const lang::SymbolFact* class_sym,
      std::string_view method_name) const;

private:
  std::vector<std::string> python_source_roots_{"", "src"};
  std::unordered_map<std::string, std::unordered_set<std::string>> reverse_imports_;
  std::unordered_map<std::string, std::unordered_set<std::string>> forward_imports_;
  std::unordered_map<std::string, std::vector<std::string>> symbols_by_name_;
  std::unordered_set<std::string_view> common_names_;
  const FactStore* store_{nullptr};

  void init_common_names();

  const lang::SymbolFact* find_class_symbol(
      const FactStore& store,
      const lang::FileFacts& facts,
      std::string_view class_name,
      const std::unordered_map<std::string, ImportBinding>& imports) const;

  std::string find_constructor(
      const FactStore& store,
      const lang::SymbolFact* class_sym) const;

  std::string find_constructor_impl(
      const FactStore& store,
      const lang::SymbolFact* class_sym,
      std::unordered_set<const lang::SymbolFact*>& visited,
      int depth = 0) const;

  const lang::SymbolFact* find_method_in_class_hierarchy_impl(
      const FactStore& store,
      const lang::SymbolFact* class_sym,
      std::string_view method_name,
      std::unordered_set<const lang::SymbolFact*>& visited,
      int depth = 0) const;
};

} // namespace gs::index
