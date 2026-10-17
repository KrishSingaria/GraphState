#include "core/engine.hpp"

#include "db/schema.hpp"
#include "gate/changesets.hpp"
#include "gate/disk_txn.hpp"
#include "gate/gate.hpp"
#include "index/watcher.hpp"
#include "memory/memory_store.hpp"
#include "memory/sessions.hpp"
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
  watcher_ = std::make_unique<index::Watcher>(*this);
  gate_ = std::make_unique<gate::Gate>(*this);
  changesets_ = std::make_unique<gate::Changesets>(*this);
  sessions_ = std::make_unique<memory::Sessions>(*this);
  memory_store_ = std::make_unique<memory::MemoryStore>(*this);
}

Engine::~Engine() {
  if (watcher_) {
    watcher_->stop();
  }
}

void Engine::initialize() {
  gate::DiskTxn::recover_startup(config_.root, client_, config_.project_graph());
  if (client_.ping()) {
    try {
      sessions_->mark_interrupted_on_startup();
      changesets_->load_on_startup();
      db::bootstrap_project_schema(client_, config_.project_graph());
      db::bootstrap_user_schema(client_, config_.user_graph());
      db::ensure_meta(client_, config_.project_graph(), platform::to_utf8(config_.root), config_.workspace_key());
    } catch (...) {}
  }
  try {
    indexer_->startup_reconcile();
  } catch (...) {}

  if (config_.watch.enabled && watcher_) {
    watcher_->start();
  }
}

void Engine::push_notice(nlohmann::json notice) {
  std::lock_guard<std::mutex> lock(notice_mu_);
  notice_queue_.push_back(std::move(notice));
}

std::vector<nlohmann::json> Engine::drain_notices() {
  std::lock_guard<std::mutex> lock(notice_mu_);
  std::vector<nlohmann::json> drained = std::move(notice_queue_);
  notice_queue_.clear();
  return drained;
}

bool Engine::has_notices() const {
  std::lock_guard<std::mutex> lock(notice_mu_);
  return !notice_queue_.empty();
}

} // namespace gs
