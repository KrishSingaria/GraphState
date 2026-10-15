#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "lang/facts.hpp"

namespace gs {
class Engine;
}

namespace gs::index {

struct IndexReport {
  size_t scanned = 0;
  size_t parsed = 0;
  size_t changed = 0;
  size_t deleted = 0;
  size_t skipped = 0;
  double ms = 0.0;
};

class Indexer {
public:
  explicit Indexer(Engine& engine);
  ~Indexer() = default;

  // Non-copyable
  Indexer(const Indexer&) = delete;
  Indexer& operator=(const Indexer&) = delete;

  // Startup reconcile: scan -> parse parallel -> diff hashes -> sync changed -> delete deleted -> resolve calls
  IndexReport startup_reconcile();

  // Full rebuild: Q_CODE_WIPE + startup_reconcile
  IndexReport full_rebuild();

  // Reindex specific files (watcher / reindex tool / ensure_fresh)
  IndexReport reindex(const std::vector<std::string>& files, std::string_view actor = "agent");

  // Apply files written by Gate P6
  void apply_written(const std::vector<lang::FileFacts>& post, const std::vector<std::string>& deleted);

  // Lazy freshness check: returns true if file was stale and reindexed
  bool ensure_fresh(const std::string& file);

private:
  Engine& engine_;
};

} // namespace gs::index
