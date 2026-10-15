#include "core/engine.hpp"

#include "db/schema.hpp"
#include "platform/platform.hpp"

namespace gs {

Engine::Engine(core::Config config)
    : config_(std::move(config)),
      client_(config_.falkordb.host,
              config_.falkordb.port,
              config_.falkordb.password,
              config_.falkordb.connect_timeout_ms,
              config_.falkordb.query_timeout_ms),
      fact_store_(config_.root),
      resolver_(config_.python.source_roots) {
  fact_store_.set_case_insensitive(platform::fs_case_insensitive(config_.root));
  graph_writer_ = std::make_unique<index::GraphWriter>(client_, config_.project_graph());
  indexer_ = std::make_unique<index::Indexer>(*this);
}

Engine::~Engine() = default;

void Engine::initialize() {
  db::bootstrap_project_schema(client_, config_.project_graph());
  db::bootstrap_user_schema(client_, config_.user_graph());
  db::ensure_meta(client_, config_.project_graph(), platform::to_utf8(config_.root), config_.workspace_key());
}

} // namespace gs
