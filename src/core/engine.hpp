#pragma once

#include <memory>
#include <mutex>
#include <string>
#include "core/config.hpp"
#include "db/graph_client.hpp"
#include "index/fact_store.hpp"
#include "index/graph_writer.hpp"
#include "index/indexer.hpp"
#include "index/resolver.hpp"

namespace gs::gate {
class Gate;
class Changesets;
}

namespace gs::index {
class Watcher;
}

namespace gs::memory {
class Sessions;
class MemoryStore;
}

namespace gs {

class Engine {
public:
  explicit Engine(core::Config config);
  ~Engine();

  // Non-copyable, non-movable
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  const core::Config& config() const { return config_; }
  core::Config& config() { return config_; }

  db::GraphClient& client() { return client_; }
  index::FactStore& fact_store() { return fact_store_; }
  const index::FactStore& fact_store() const { return fact_store_; }

  index::Resolver& resolver() { return resolver_; }
  const index::Resolver& resolver() const { return resolver_; }

  index::GraphWriter& graph_writer() { return *graph_writer_; }
  index::Indexer& indexer() { return *indexer_; }

  index::Watcher& watcher() { return *watcher_; }
  const index::Watcher& watcher() const { return *watcher_; }

  gate::Gate& gate() { return *gate_; }
  const gate::Gate& gate() const { return *gate_; }

  gate::Changesets& changesets() { return *changesets_; }
  const gate::Changesets& changesets() const { return *changesets_; }

  memory::Sessions& sessions() { return *sessions_; }
  const memory::Sessions& sessions() const { return *sessions_; }

  memory::MemoryStore& memory_store() { return *memory_store_; }
  const memory::MemoryStore& memory_store() const { return *memory_store_; }

  std::mutex& state_mutex() { return state_mu_; }

  // Notice Queue
  void push_notice(nlohmann::json notice);
  std::vector<nlohmann::json> drain_notices();
  bool has_notices() const;

  // Bootstrap schema and initialize engine metadata
  void initialize();

private:
  core::Config config_;
  db::GraphClient client_;
  index::FactStore fact_store_;
  index::Resolver resolver_;
  std::unique_ptr<index::GraphWriter> graph_writer_;
  std::unique_ptr<index::Indexer> indexer_;
  std::unique_ptr<index::Watcher> watcher_;
  std::unique_ptr<gate::Gate> gate_;
  std::unique_ptr<gate::Changesets> changesets_;
  std::unique_ptr<memory::Sessions> sessions_;
  std::unique_ptr<memory::MemoryStore> memory_store_;
  std::mutex state_mu_;

  mutable std::mutex notice_mu_;
  std::vector<nlohmann::json> notice_queue_;
};

} // namespace gs
