#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "db/graph_client.hpp"
#include "index/resolver.hpp"
#include "lang/facts.hpp"

namespace gs::index {

class GraphWriter {
public:
  GraphWriter(db::GraphClient& client, std::string graph);

  // Sync a batch of files (transactions of <= 50 files per MULTI/EXEC batch)
  void sync_files(const std::vector<const lang::FileFacts*>& files,
                  const std::vector<ResolvedImport>& resolved_imports,
                  const std::vector<ResolvedInherit>& resolved_inherits,
                  int64_t now);

  void sync_files(const std::vector<const lang::FileFacts*>& files,
                  const std::vector<ResolvedImport>& resolved_imports,
                  int64_t now);

  // Sync overload when imports/inherits are empty or handled externally
  void sync_files(const std::vector<const lang::FileFacts*>& files, int64_t now);

  // Delete files and their functions/classes removed from disk
  void delete_files(const std::vector<std::string>& files);

  // Replace calls for affected files (rows chunked into batches of <= 2000)
  void replace_calls(const std::vector<std::string>& affected_files,
                     const std::vector<ResolvedCall>& calls);

  // Wipe all code subgraph elements (Q_CODE_WIPE)
  void wipe_code_graph();

  // Update last_indexed_at (Q_META_TOUCH_INDEXED)
  void touch_indexed(int64_t now);

  // Relink memory/pitfalls (Q_RELINK_PITFALLS, Q_RELINK_MEMORIES)
  void relink_memory(const std::vector<std::string>& symbol_ids);

  const std::string& graph_name() const { return graph_; }

private:
  db::GraphClient& client_;
  std::string graph_;
};

} // namespace gs::index
