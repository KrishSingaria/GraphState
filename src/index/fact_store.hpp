#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "lang/facts.hpp"

namespace gs::index {

class FactStore {
public:
  explicit FactStore(std::filesystem::path root = {});
  ~FactStore() = default;

  // Non-copyable, movable
  FactStore(const FactStore&) = delete;
  FactStore& operator=(const FactStore&) = delete;
  FactStore(FactStore&&) noexcept;
  FactStore& operator=(FactStore&&) noexcept;

  // Configuration
  void set_root(std::filesystem::path root);
  const std::filesystem::path& root() const;
  void set_case_insensitive(bool ci);
  bool is_case_insensitive() const;

  // File fact mutations (unique_lock)
  void put(lang::FileFacts facts);
  void erase(std::string_view rel_path);
  void clear();

  // File fact accessors (shared_lock)
  const lang::FileFacts* get(std::string_view rel_path) const;
  std::optional<lang::FileFacts> get_copy(std::string_view rel_path) const;
  bool contains(std::string_view rel_path) const;
  std::vector<std::string> all_files() const;
  std::vector<std::string> files() const; // alias for all_files()
  size_t file_count() const;

  // Symbol queries (shared_lock)
  const lang::SymbolFact* symbol(std::string_view id) const; // O(1) by symbol ID
  const lang::SymbolFact* find_symbol_by_id(std::string_view id) const; // alias
  std::vector<const lang::SymbolFact*> find_symbols_by_name(std::string_view name) const;
  std::vector<const lang::SymbolFact*> find_symbols_by_qualname(std::string_view qualname) const;
  std::vector<const lang::SymbolFact*> find_symbols(std::string_view query, size_t limit = 50) const;
  size_t total_symbols() const;

  // Freshness check (shared_lock on store, non-blocking disk stat)
  bool is_stale(std::string_view rel_path) const;
  bool is_stale(const std::filesystem::path& root, std::string_view rel_path) const;

private:
  using SymbolLocation = std::pair<std::string, size_t>; // (file_path, index_in_facts)

  mutable std::shared_mutex mutex_;
  std::filesystem::path root_;
  bool case_insensitive_ = false;

  std::unordered_map<std::string, lang::FileFacts> files_;
  std::unordered_map<std::string, SymbolLocation> symbol_index_;      // id -> (file, idx)
  std::unordered_multimap<std::string, SymbolLocation> by_name_;     // name -> (file, idx)
  std::unordered_multimap<std::string, SymbolLocation> by_qualname_; // qualname -> (file, idx)
  std::unordered_map<std::string, std::string> lower_files_;         // lowercase -> exact

  void unindex_file_locked(const std::string& file);
  std::string resolve_file_key_locked(std::string_view rel_path) const;
};

} // namespace gs::index
