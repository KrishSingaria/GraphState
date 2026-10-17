#include "index/indexer.hpp"

#include <chrono>
#include <fstream>
#include <thread>
#include <unordered_set>
#include "core/clock.hpp"
#include "core/engine.hpp"
#include "core/hash.hpp"
#include "db/queries.hpp"
#include "index/scanner.hpp"
#include "lang/extractor.hpp"
#include "platform/platform.hpp"

namespace gs::index {

Indexer::Indexer(Engine& engine) : engine_(engine) {}

IndexReport Indexer::startup_reconcile() {
  auto t0 = std::chrono::steady_clock::now();
  IndexReport report;

  // 1. Scan workspace
  auto scanned = scan(engine_.config(), engine_.config().root);
  report.scanned = scanned.size();

  // 2. Parallel parse
  std::vector<lang::FileFacts> all_facts(scanned.size());
  int num_threads = engine_.config().effective_parse_threads();
  if (num_threads < 1) num_threads = 1;
  if (num_threads > static_cast<int>(scanned.size())) num_threads = static_cast<int>(scanned.size());
  if (num_threads == 0) num_threads = 1;

  auto worker = [&](size_t start, size_t end) {
    for (size_t i = start; i < end; ++i) {
      const auto& item = scanned[i];
      auto full_path = engine_.config().root / platform::from_utf8(item.rel);

      std::string content;
      std::ifstream ifs(full_path, std::ios::binary);
      if (ifs) {
        content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
      }

      auto lang = lang::LanguageRegistry::get().for_path(item.rel).lang;
      all_facts[i] = lang::extract_any(item.rel, content, lang);
      all_facts[i].mtime_ns = item.mtime_ns;
      all_facts[i].size = item.size;
    }
  };

  if (num_threads <= 1 || scanned.size() <= 1) {
    worker(0, scanned.size());
  } else {
    std::vector<std::thread> threads;
    size_t chunk_size = (scanned.size() + num_threads - 1) / num_threads;
    for (int t = 0; t < num_threads; ++t) {
      size_t start = t * chunk_size;
      size_t end = std::min(start + chunk_size, scanned.size());
      if (start < end) {
        threads.emplace_back(worker, start, end);
      }
    }
    for (auto& th : threads) {
      if (th.joinable()) th.join();
    }
  }

  // 3. Populate FactStore
  engine_.fact_store().clear();
  for (auto& f : all_facts) {
    engine_.fact_store().put(std::move(f));
  }
  report.parsed = engine_.fact_store().file_count();

  // 4. Fetch graph hashes
  std::unordered_map<std::string, std::string> db_hashes;
  try {
    auto reply = engine_.client().rows(
        engine_.config().project_graph(),
        db::Query{"file_hashes", db::Q_FILE_HASHES, nlohmann::json::object(), true});

    if (!reply.empty() && reply[0].is_array()) {
      for (const auto& item : reply[0]) {
        if (item.contains("id") && item.contains("hash")) {
          db_hashes[item["id"].get<std::string>()] = item["hash"].get<std::string>();
        }
      }
    }
  } catch (...) {
    // If graph does not exist or error, assume clean state
  }

  // 5. Diff changed and deleted
  std::vector<std::string> changed;
  std::vector<std::string> deleted;

  for (const auto& f : engine_.fact_store().all_files()) {
    const auto* facts = engine_.fact_store().get(f);
    if (!facts) continue;
    auto it = db_hashes.find(f);
    if (it == db_hashes.end() || it->second != facts->hash) {
      changed.push_back(f);
    }
  }

  for (const auto& [f, h] : db_hashes) {
    if (!engine_.fact_store().contains(f)) {
      deleted.push_back(f);
    }
  }

  report.changed = changed.size();
  report.deleted = deleted.size();

  // 6. Rebuild resolver indices
  engine_.resolver().rebuild_indices(engine_.fact_store());

  // 7. Delete deleted files
  if (engine_.client().ping()) {
    if (!deleted.empty()) {
      engine_.graph_writer().delete_files(deleted);
    }
  }

  // 8. Sync changed files
  int64_t now = core::now_ms();
  if (engine_.client().ping()) {
    if (!changed.empty()) {
      std::vector<const lang::FileFacts*> changed_facts;
      std::vector<index::ResolvedImport> all_imports;
      std::vector<index::ResolvedInherit> all_inherits;

      for (const auto& f : changed) {
        const auto* facts = engine_.fact_store().get(f);
        if (facts) {
          changed_facts.push_back(facts);
          auto imps = engine_.resolver().resolve_imports_for_file(engine_.fact_store(), *facts);
          all_imports.insert(all_imports.end(), imps.begin(), imps.end());
          auto inhs = engine_.resolver().resolve_inherits_for_file(engine_.fact_store(), *facts);
          all_inherits.insert(all_inherits.end(), inhs.begin(), inhs.end());
        }
      }

      engine_.graph_writer().sync_files(changed_facts, all_imports, all_inherits, now);
    }
  }

  // 9. Call resolution for affected files
  std::vector<std::string> affected;
  if (db_hashes.empty() || changed.size() == engine_.fact_store().file_count()) {
    affected = engine_.fact_store().all_files();
  } else {
    std::unordered_set<std::string> aff_set(changed.begin(), changed.end());
    for (const auto& f : changed) {
      for (const auto& dep : engine_.resolver().dependents_of(f)) {
        aff_set.insert(dep);
      }
    }
    for (const auto& f : deleted) {
      for (const auto& dep : engine_.resolver().dependents_of(f)) {
        aff_set.insert(dep);
      }
    }
    affected.assign(aff_set.begin(), aff_set.end());
  }

  std::vector<ResolvedCall> all_calls;
  for (const auto& f : affected) {
    auto calls = engine_.resolver().resolve_file(engine_.fact_store(), f);
    all_calls.insert(all_calls.end(), calls.begin(), calls.end());
  }

  if (engine_.client().ping()) {
    engine_.graph_writer().replace_calls(affected, all_calls);
    engine_.graph_writer().touch_indexed(now);
  }

  auto t1 = std::chrono::steady_clock::now();
  report.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return report;
}

IndexReport Indexer::full_rebuild() {
  engine_.graph_writer().wipe_code_graph();
  return startup_reconcile();
}

IndexReport Indexer::reindex(const std::vector<std::string>& files, std::string_view /*actor*/) {
  auto t0 = std::chrono::steady_clock::now();
  IndexReport report;
  report.scanned = files.size();

  std::vector<std::string> changed;
  std::vector<std::string> deleted;

  for (const auto& f : files) {
    auto full_path = engine_.config().root / platform::from_utf8(f);
    if (std::filesystem::exists(full_path)) {
      std::string content;
      std::ifstream ifs(full_path, std::ios::binary);
      if (ifs) {
        content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
      }
      auto stat = platform::stat(full_path);
      auto lang = lang::LanguageRegistry::get().for_path(f).lang;
      auto facts = lang::extract_any(f, content, lang);
      facts.mtime_ns = stat.mtime_ns;
      facts.size = stat.size;

      engine_.fact_store().put(std::move(facts));
      changed.push_back(f);
    } else {
      engine_.fact_store().erase(f);
      deleted.push_back(f);
    }
  }

  report.changed = changed.size();
  report.deleted = deleted.size();

  if (changed.size() + deleted.size() <= 4) {
    for (const auto& f : changed) {
      engine_.resolver().update_file(f, engine_.fact_store());
    }
    for (const auto& f : deleted) {
      engine_.resolver().remove_file(f);
    }
  } else {
    engine_.resolver().rebuild_indices(engine_.fact_store());
  }

  if (engine_.client().ping()) {
    if (!deleted.empty()) {
      engine_.graph_writer().delete_files(deleted);
    }

    int64_t now = core::now_ms();
    if (!changed.empty()) {
      std::vector<const lang::FileFacts*> changed_facts;
      std::vector<index::ResolvedImport> all_imports;
      std::vector<index::ResolvedInherit> all_inherits;

      for (const auto& f : changed) {
        const auto* facts = engine_.fact_store().get(f);
        if (facts) {
          changed_facts.push_back(facts);
          auto imps = engine_.resolver().resolve_imports_for_file(engine_.fact_store(), *facts);
          all_imports.insert(all_imports.end(), imps.begin(), imps.end());
          auto inhs = engine_.resolver().resolve_inherits_for_file(engine_.fact_store(), *facts);
          all_inherits.insert(all_inherits.end(), inhs.begin(), inhs.end());
        }
      }

      engine_.graph_writer().sync_files(changed_facts, all_imports, all_inherits, now);
    }

    std::unordered_set<std::string> aff_set(changed.begin(), changed.end());
    for (const auto& f : changed) {
      for (const auto& dep : engine_.resolver().dependents_of(f)) {
        aff_set.insert(dep);
      }
    }
    for (const auto& f : deleted) {
      for (const auto& dep : engine_.resolver().dependents_of(f)) {
        aff_set.insert(dep);
      }
    }
    std::vector<std::string> affected(aff_set.begin(), aff_set.end());

    std::vector<ResolvedCall> all_calls;
    for (const auto& f : affected) {
      auto calls = engine_.resolver().resolve_file(engine_.fact_store(), f);
      all_calls.insert(all_calls.end(), calls.begin(), calls.end());
    }
    engine_.graph_writer().replace_calls(affected, all_calls);

    engine_.graph_writer().touch_indexed(now);
  }

  auto t1 = std::chrono::steady_clock::now();
  report.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return report;
}

void Indexer::apply_written(const std::vector<lang::FileFacts>& post, const std::vector<std::string>& deleted) {
  for (const auto& f : deleted) {
    engine_.fact_store().erase(f);
    engine_.resolver().remove_file(f);
  }

  for (const auto& facts : post) {
    engine_.fact_store().put(facts);
  }

  engine_.resolver().rebuild_indices(engine_.fact_store());

  if (!deleted.empty()) {
    engine_.graph_writer().delete_files(deleted);
  }

  int64_t now = core::now_ms();
  std::vector<const lang::FileFacts*> post_ptrs;
  std::vector<ResolvedImport> all_imports;
  std::vector<ResolvedInherit> all_inherits;

  for (const auto& facts : post) {
    post_ptrs.push_back(engine_.fact_store().get(facts.file));
    auto imps = engine_.resolver().resolve_imports_for_file(engine_.fact_store(), facts);
    all_imports.insert(all_imports.end(), imps.begin(), imps.end());
    auto inhs = engine_.resolver().resolve_inherits_for_file(engine_.fact_store(), facts);
    all_inherits.insert(all_inherits.end(), inhs.begin(), inhs.end());
  }

  if (!post_ptrs.empty()) {
    engine_.graph_writer().sync_files(post_ptrs, all_imports, all_inherits, now);
  }

  std::unordered_set<std::string> aff_set;
  for (const auto& f : post) {
    aff_set.insert(f.file);
    for (const auto& dep : engine_.resolver().dependents_of(f.file)) {
      aff_set.insert(dep);
    }
  }
  for (const auto& f : deleted) {
    for (const auto& dep : engine_.resolver().dependents_of(f)) {
      aff_set.insert(dep);
    }
  }
  std::vector<std::string> affected(aff_set.begin(), aff_set.end());

  std::vector<ResolvedCall> all_calls;
  for (const auto& f : affected) {
    auto calls = engine_.resolver().resolve_file(engine_.fact_store(), f);
    all_calls.insert(all_calls.end(), calls.begin(), calls.end());
  }
  engine_.graph_writer().replace_calls(affected, all_calls);

  engine_.graph_writer().touch_indexed(now);
}

bool Indexer::ensure_fresh(const std::string& file) {
  if (!engine_.fact_store().is_stale(file)) {
    return false;
  }

  auto full_path = engine_.config().root / platform::from_utf8(file);
  if (!std::filesystem::exists(full_path)) {
    reindex({file}, "external");
    return true;
  }

  std::string content;
  std::ifstream ifs(full_path, std::ios::binary);
  if (ifs) {
    content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  }

  std::string h = core::xxh3_hex(content);
  const auto* existing = engine_.fact_store().get(file);
  if (existing && existing->hash == h) {
    return false;
  }

  reindex({file}, "external");
  return true;
}

} // namespace gs::index
