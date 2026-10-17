#include "index/watcher.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include "core/clock.hpp"
#include "core/config.hpp"
#include "core/engine.hpp"
#include "core/hash.hpp"
#include "db/queries.hpp"
#include "gate/changesets.hpp"
#include "gate/gate.hpp"
#include "index/scanner.hpp"
#include "lang/extractor.hpp"
#include "lang/language.hpp"
#include "memory/sessions.hpp"
#include "platform/platform.hpp"

namespace gs::index {

Watcher::Watcher(Engine& engine) : engine_(engine) {}

Watcher::~Watcher() {
  stop();
}

void Watcher::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  thread_ = std::thread(&Watcher::loop, this);
}

void Watcher::stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Watcher::loop() {
  while (running_.load()) {
    {
      std::unique_lock<std::mutex> lock(cv_mu_);
      int interval = engine_.config().watch.interval_ms;
      if (interval <= 0) interval = 500;
      cv_.wait_for(lock, std::chrono::milliseconds(interval), [this] {
        return !running_.load();
      });
    }
    if (!running_.load()) break;
    try {
      tick();
    } catch (...) {
      // Background loop should never crash on transient FS/DB exceptions
    }
  }
}

void Watcher::tick() {
  int64_t now_ms = core::now_ms();

  // 1. Live reload .graphstate/rules.json if changed
  auto rules_path = engine_.config().root / ".graphstate" / "rules.json";
  auto rules_st = platform::stat(rules_path);
  if (rules_st.exists && rules_st.mtime_ns != last_rules_mtime_) {
    last_rules_mtime_ = rules_st.mtime_ns;
    engine_.gate().rules_engine().load_config(engine_.config().root);
  }

  // 2. Check git HEAD / index for bulk VCS changes
  auto head_p = engine_.config().root / ".git" / "HEAD";
  auto idx_p = engine_.config().root / ".git" / "index";
  auto head_st = platform::stat(head_p);
  auto idx_st = platform::stat(idx_p);
  bool git_changed = false;
  if (last_git_head_mtime_ != 0 && head_st.mtime_ns != 0 && head_st.mtime_ns != last_git_head_mtime_) {
    git_changed = true;
  }
  if (last_git_index_mtime_ != 0 && idx_st.mtime_ns != 0 && idx_st.mtime_ns != last_git_index_mtime_) {
    git_changed = true;
  }
  last_git_head_mtime_ = head_st.mtime_ns;
  last_git_index_mtime_ = idx_st.mtime_ns;

  // 3. Periodic directory rescan to detect newly created files
  std::vector<std::string> new_files;
  if (last_rescan_ts_ == 0 || (now_ms - last_rescan_ts_) >= engine_.config().watch.rescan_interval_ms) {
    last_rescan_ts_ = now_ms;
    auto scanned = index::scan(engine_.config(), engine_.config().root);
    for (const auto& item : scanned) {
      if (!engine_.fact_store().contains(item.rel)) {
        new_files.push_back(item.rel);
      }
    }
  }

  // 4. Stat all files in FactStore
  std::vector<std::string> changed;
  std::vector<std::string> vanished;
  auto all_known = engine_.fact_store().all_files();
  for (const auto& f : all_known) {
    auto full_p = engine_.config().root / platform::from_utf8(f);
    auto st = platform::stat(full_p);
    if (!st.exists) {
      vanished.push_back(f);
    } else {
      const auto* cur_facts = engine_.fact_store().get(f);
      if (cur_facts) {
        bool racy = (racy_clean_files_.find(f) != racy_clean_files_.end());
        if (racy || st.size != cur_facts->size || st.mtime_ns != cur_facts->mtime_ns) {
          changed.push_back(f);
        }
      }
    }
  }
  for (const auto& nf : new_files) {
    changed.push_back(nf);
  }
  std::sort(changed.begin(), changed.end());
  changed.erase(std::unique(changed.begin(), changed.end()), changed.end());

  // 5. Bulk change handling (§6.15 / §5.13)
  size_t total_changes = changed.size() + vanished.size();
  if (git_changed || (engine_.config().watch.bulk_threshold > 0 &&
                      total_changes > static_cast<size_t>(engine_.config().watch.bulk_threshold))) {
    std::unique_lock<std::mutex> lock(engine_.state_mutex());
    engine_.indexer().startup_reconcile();
    racy_clean_files_.clear();

    std::vector<std::string> all_files = changed;
    all_files.insert(all_files.end(), vanished.begin(), vanished.end());
    std::string seed = "bulk|" + std::to_string(now_ms);
    std::string patch_id = "x_" + core::xxh3_hex(seed).substr(0, 12);

    if (engine_.client().ping()) {
      try {
        engine_.client().exec(engine_.config().project_graph(), db::Query{
            .name = "decision_record_synced",
            .cypher = db::Q_DECISION_RECORD,
            .params = {
                {"d", {
                    {"patch_id", patch_id},
                    {"status", "SYNCED"},
                    {"actor", "external"},
                    {"intent", "External VCS change"},
                    {"reasoning", "Bulk external change detected"},
                    {"failed_phase", ""},
                    {"violation_codes", nlohmann::json::array()},
                    {"files", all_files},
                    {"symbols", nlohmann::json::array()},
                    {"ts", now_ms},
                    {"duration_ms", 0}
                }},
                {"session", ""},
                {"intent_id", ""}
            },
            .read_only = false
        });
      } catch (...) {}
    }

    engine_.push_notice({
        {"kind", "bulk_change"},
        {"files", total_changes},
        {"likely", "vcs"}
    });

    auto cs_opt = engine_.changesets().current();
    if (cs_opt.has_value() && cs_opt->status == "OPEN") {
      engine_.changesets().recompute();
    }
    return;
  }

  // 6. Non-bulk changed or vanished files
  if (changed.empty() && vanished.empty()) {
    return;
  }

  std::unique_lock<std::mutex> lock(engine_.state_mutex());
  std::vector<std::string> genuinely_changed;
  std::unordered_map<std::string, std::string> new_contents;

  for (const auto& f : changed) {
    auto full_p = engine_.config().root / platform::from_utf8(f);
    auto st = platform::stat(full_p);
    if (!st.exists) {
      vanished.push_back(f);
      continue;
    }

    std::string content;
    std::ifstream ifs(full_p, std::ios::binary);
    if (ifs) {
      content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    std::string h = core::xxh3_hex(content);
    const auto* cur = engine_.fact_store().get(f);
    if (cur && cur->hash == h) {
      // Hash unchanged - update stat only (touch or metadata change)
      racy_clean_files_.erase(f);
      continue;
    }

    genuinely_changed.push_back(f);
    new_contents[f] = std::move(content);

    // Racy-clean rule (§12.8): files hashed < 2s after mtime get checked once more next tick
    int64_t mtime_ms = st.mtime_ns / 1'000'000;
    if (std::abs(now_ms - mtime_ms) < 2000) {
      racy_clean_files_.insert(f);
    } else {
      racy_clean_files_.erase(f);
    }
  }

  if (genuinely_changed.empty() && vanished.empty()) {
    return;
  }

  // Build working set for Gate audit
  std::map<std::string, gate::Buffer> ws;
  for (const auto& f : genuinely_changed) {
    gate::Buffer buf;
    buf.lang = lang::LanguageRegistry::get().for_path(f).lang;
    const auto* cur = engine_.fact_store().get(f);
    if (cur) {
      buf.pre_facts = *cur;
    } else {
      buf.is_new = true;
    }
    buf.post = new_contents[f];
    buf.post_facts = lang::extract_any(f, *buf.post, buf.lang);
    auto st = platform::stat(engine_.config().root / platform::from_utf8(f));
    buf.post_facts.mtime_ns = st.mtime_ns;
    buf.post_facts.size = st.size;
    ws[f] = std::move(buf);
  }
  for (const auto& f : vanished) {
    gate::Buffer buf;
    buf.lang = lang::LanguageRegistry::get().for_path(f).lang;
    const auto* cur = engine_.fact_store().get(f);
    if (cur) {
      buf.pre_facts = *cur;
    }
    buf.is_deleted = true;
    ws[f] = std::move(buf);
  }

  // Audit P2-P4
  std::vector<gate::Violation> violations = engine_.gate().audit_cumulative(ws);

  // Syntax check
  for (const auto& [f, buf] : ws) {
    if (!buf.is_deleted && !buf.post_facts.parse_ok) {
      violations.push_back(gate::Violation{
          .code = "SYNTAX_ERROR",
          .severity = gate::Severity::Error,
          .phase = "SYNTAX",
          .message = f + " has syntax errors",
          .location = gate::Location{
              .file = f,
              .line = 1,
              .col = 1
          }
      });
    }
  }

  std::vector<std::string> error_codes;
  for (const auto& v : violations) {
    if (v.severity == gate::Severity::Error) {
      error_codes.push_back(v.code);
    }
  }

  // Re-index modified and vanished files in FactStore and Graph
  std::vector<std::string> all_files = genuinely_changed;
  all_files.insert(all_files.end(), vanished.begin(), vanished.end());
  engine_.indexer().reindex(all_files, "external");

  // If audit findings include errors, or session is currently active, record ungated decision & push notice
  bool session_active = engine_.sessions().is_active();
  if (!error_codes.empty() || session_active) {
    std::string seed = "ungated|" + std::to_string(now_ms);
    std::string patch_id = "x_" + core::xxh3_hex(seed).substr(0, 12);
    std::string current_session = engine_.sessions().ctx().session_id;
    std::string current_intent = engine_.sessions().ctx().current_intent_id;

    if (engine_.client().ping()) {
      try {
        engine_.client().exec(engine_.config().project_graph(), db::Query{
            .name = "decision_record_ungated",
            .cypher = db::Q_DECISION_RECORD,
            .params = {
                {"d", {
                    {"patch_id", patch_id},
                    {"status", "UNGATED"},
                    {"actor", "external"},
                    {"intent", "External modification"},
                    {"reasoning", "File modified out-of-band"},
                    {"failed_phase", error_codes.empty() ? "" : "AUDIT"},
                    {"violation_codes", error_codes},
                    {"files", all_files},
                    {"symbols", nlohmann::json::array()},
                    {"ts", now_ms},
                    {"duration_ms", 0}
                }},
                {"session", current_session},
                {"intent_id", current_intent}
            },
            .read_only = false
        });
      } catch (...) {}
    }

    nlohmann::json notice = {
        {"kind", "ungated_edit"},
        {"patch_id", patch_id},
        {"files", all_files},
        {"violation_codes", error_codes}
    };
    if (!violations.empty()) {
      nlohmann::json viols_arr = nlohmann::json::array();
      for (const auto& v : violations) {
        viols_arr.push_back(gate::to_json(v));
      }
      notice["violations"] = std::move(viols_arr);
    }
    engine_.push_notice(std::move(notice));
  }

  // If active changeset touches any changed/vanished file, recompute obligations
  auto cs_opt = engine_.changesets().current();
  if (cs_opt.has_value() && cs_opt->status == "OPEN") {
    bool touches = false;
    for (const auto& f : all_files) {
      for (const auto& cs_f : cs_opt->files) {
        if (f == cs_f) {
          touches = true;
          break;
        }
      }
      if (touches) break;
    }
    if (touches) {
      engine_.changesets().recompute();
    }
  }
}

} // namespace gs::index
