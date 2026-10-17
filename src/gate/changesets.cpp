#include "gate/changesets.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/clock.hpp"
#include "core/engine.hpp"
#include "core/error.hpp"
#include "core/hash.hpp"
#include "db/queries.hpp"
#include "gate/gate.hpp"
#include "index/fact_store.hpp"
#include "lang/extractor.hpp"
#include "lang/language.hpp"
#include "mcp/tool_registry.hpp"
#include "memory/sessions.hpp"
#include "platform/platform.hpp"

namespace gs::gate {

namespace {

constexpr std::string_view Q_FORCED_PITFALL =
    "MERGE (p:Pitfall {key: $key})\n"
    "ON CREATE SET p.code = $code, p.rule_id = $rule_id, p.symbol_id = $symbol_id, p.file = $file,\n"
    "              p.lesson = $lesson, p.count = 0, p.resolved_count = 0, p.first_seen = $now, p.last_resolved = 0\n"
    "SET p.count = p.count + 1, p.last_seen = $now, p.lesson = $lesson";

std::string generate_id(std::string_view prefix) {
  int64_t now = core::now_ms();
  static uint64_t counter = 0;
  ++counter;
  std::string seed = std::to_string(now) + "_" + std::to_string(counter);
  return std::string(prefix) + core::xxh3_hex(seed).substr(0, 12);
}

std::optional<std::string> read_file_string(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f.is_open()) return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

} // namespace

nlohmann::json to_json(const Obligation& ob) {
  nlohmann::json j = {
      {"id", ob.id},
      {"code", ob.violation.code},
      {"message", ob.violation.message},
      {"fix_hint", ob.violation.fix_hint}
  };
  if (ob.violation.location.has_value()) {
    j["location"] = to_json(*ob.violation.location);
  }
  if (ob.violation.symbol_id.has_value()) {
    j["symbol_id"] = *ob.violation.symbol_id;
  }
  if (!ob.violation.evidence.empty()) {
    j["evidence"] = ob.violation.evidence;
  }
  return j;
}

nlohmann::json to_json(const ChangesetState& cs) {
  nlohmann::json obs = nlohmann::json::array();
  for (const auto& o : cs.obligations) {
    obs.push_back(to_json(o));
  }

  nlohmann::json j = {
      {"id", cs.id},
      {"status", cs.status}
  };
  if (!cs.intent.empty()) j["intent"] = cs.intent;
  if (!cs.files.empty()) j["files"] = cs.files;
  j["open_obligations"] = cs.obligations.size();
  j["obligations"] = std::move(obs);
  j["discharged"] = cs.discharged;
  if (!cs.conflicts.empty()) j["conflicts"] = cs.conflicts;
  if (!cs.message.empty()) j["message"] = cs.message;
  if (cs.created_at > 0) j["created_at"] = cs.created_at;
  if (cs.updated_at > 0) j["updated_at"] = cs.updated_at;
  return j;
}

bool Changesets::is_deferrable_code(std::string_view code) {
  return code == "CALLER_INCOMPATIBLE" ||
         code == "REMOVED_SYMBOL_IN_USE" ||
         code == "BROKEN_IMPORT" ||
         code == "CALL_INCOMPATIBLE" ||
         code == "MISSING_SYMBOL" ||
         code == "MISSING_MODULE";
}

Changesets::Changesets(Engine& engine) : engine_(engine) {}

Changesets::~Changesets() = default;

std::filesystem::path Changesets::changeset_dir(const std::string& id) const {
  return engine_.config().root / ".graphstate" / "changesets" / id;
}

std::filesystem::path Changesets::manifest_path(const std::string& id) const {
  return changeset_dir(id) / "manifest.json";
}

std::filesystem::path Changesets::base_file_path(const std::string& id, const std::string& rel_file) const {
  return changeset_dir(id) / "base" / platform::from_utf8(rel_file);
}

void Changesets::save_manifest() {
  if (manifest_.id.empty()) return;
  std::filesystem::path mpath = manifest_path(manifest_.id);
  std::filesystem::create_directories(mpath.parent_path());

  nlohmann::json j;
  j["id"] = manifest_.id;
  j["intent"] = manifest_.intent;
  j["status"] = manifest_.status;
  j["created_at"] = manifest_.created_at;

  nlohmann::json files_j = nlohmann::json::object();
  for (const auto& [f, e] : manifest_.files) {
    files_j[f] = {
        {"existed", e.existed},
        {"base_hash", e.base_hash},
        {"last_written_hash", e.last_written_hash}
    };
  }
  j["files"] = files_j;
  j["created_dirs"] = manifest_.created_dirs;

  platform::write_file_durable(mpath, j.dump(2));
}

bool Changesets::load_manifest(const std::string& id) {
  std::filesystem::path mpath = manifest_path(id);
  auto text = read_file_string(mpath);
  if (!text.has_value()) return false;

  try {
    auto j = nlohmann::json::parse(*text);
    manifest_.id = j.value("id", id);
    manifest_.intent = j.value("intent", "");
    manifest_.status = j.value("status", "OPEN");
    manifest_.created_at = j.value("created_at", 0LL);
    manifest_.files.clear();
    manifest_.created_dirs.clear();

    if (j.contains("files") && j["files"].is_object()) {
      for (auto it = j["files"].begin(); it != j["files"].end(); ++it) {
        ChangesetFileEntry e;
        e.existed = it.value().value("existed", false);
        e.base_hash = it.value().value("base_hash", "");
        e.last_written_hash = it.value().value("last_written_hash", "");
        manifest_.files[it.key()] = std::move(e);
      }
    }

    if (j.contains("created_dirs") && j["created_dirs"].is_array()) {
      for (const auto& d : j["created_dirs"]) {
        if (d.is_string()) manifest_.created_dirs.push_back(d.get<std::string>());
      }
    }

    ChangesetState state;
    state.id = manifest_.id;
    state.intent = manifest_.intent;
    state.status = manifest_.status;
    state.created_at = manifest_.created_at;
    state.updated_at = state.created_at;
    for (const auto& [f, _] : manifest_.files) {
      state.files.push_back(f);
    }
    active_ = std::move(state);
    return true;
  } catch (...) {
    return false;
  }
}

void Changesets::load_on_startup() {
  std::lock_guard<std::mutex> lock(mu_);
  std::filesystem::path cs_base = engine_.config().root / ".graphstate" / "changesets";
  std::error_code ec;
  if (std::filesystem::exists(cs_base, ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(cs_base, ec)) {
      if (entry.is_directory()) {
        std::string dir_name = platform::to_utf8(entry.path().filename());
        if (dir_name.starts_with("c_")) {
          if (load_manifest(dir_name)) {
            if (active_.has_value() && active_->status == "OPEN") {
              // Recompute obligations
              mu_.unlock();
              recompute();
              mu_.lock();
              return;
            }
          }
        }
      }
    }
  }

  // Fallback to FalkorDB if graph has OPEN changeset
  if (engine_.client().ping()) {
    try {
      auto rows = engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "open_changeset",
          .cypher = db::Q_OPEN_CHANGESET,
          .params = {},
          .read_only = true
      });
      if (!rows.empty()) {
        const auto& r = rows[0];
        std::string id = r.value("id", "");
        if (!id.empty()) {
          load_manifest(id);
          if (active_.has_value() && active_->status == "OPEN") {
            mu_.unlock();
            recompute();
            mu_.lock();
          }
        }
      }
    } catch (...) {}
  }
}

ChangesetState Changesets::begin(std::string_view intent, const memory::SessionCtx& session) {
  std::lock_guard<std::mutex> lock(mu_);
  if (active_.has_value() && active_->status == "OPEN") {
    throw mcp::ToolError("CHANGESET_OPEN",
                         "Changeset '" + active_->id + "' is already open",
                         {{"id", active_->id},
                          {"intent", active_->intent},
                          {"open_obligations", active_->obligations.size()}});
  }

  std::string new_id = generate_id("c_");
  int64_t now = core::now_ms();

  manifest_ = ChangesetManifest{};
  manifest_.id = new_id;
  manifest_.intent = std::string(intent);
  manifest_.status = "OPEN";
  manifest_.created_at = now;

  std::filesystem::path c_dir = changeset_dir(new_id);
  std::filesystem::create_directories(c_dir / "base");
  save_manifest();

  ChangesetState st;
  st.id = new_id;
  st.intent = std::string(intent);
  st.status = "OPEN";
  st.created_at = now;
  st.updated_at = now;
  active_ = st;

  if (engine_.client().ping()) {
    try {
      engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "changeset_create",
          .cypher = db::Q_CHANGESET_CREATE,
          .params = {
              {"id", new_id},
              {"intent", std::string(intent)},
              {"session", session.session_id},
              {"now", now}
          },
          .read_only = false
      });
    } catch (...) {}
  }

  return st;
}

std::optional<ChangesetState> Changesets::current() const {
  std::lock_guard<std::mutex> lock(mu_);
  return active_;
}

void Changesets::before_write(const std::vector<std::string>& paths) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!active_.has_value() || active_->status != "OPEN") return;

  bool dirty = false;
  for (const auto& p : paths) {
    if (manifest_.files.find(p) == manifest_.files.end()) {
      std::filesystem::path full_p = engine_.config().root / platform::from_utf8(p);
      std::error_code ec;
      if (std::filesystem::exists(full_p, ec)) {
        auto content = read_file_string(full_p);
        std::string base_hash = content.has_value() ? core::xxh3_hex(*content) : "";
        std::filesystem::path base_dst = base_file_path(manifest_.id, p);
        std::filesystem::create_directories(base_dst.parent_path(), ec);
        platform::hardlink_or_copy(full_p, base_dst);
        manifest_.files[p] = ChangesetFileEntry{true, base_hash, base_hash};
      } else {
        manifest_.files[p] = ChangesetFileEntry{false, "", ""};
      }
      dirty = true;
    }

    // Check if path has directories that don't exist yet
    std::filesystem::path rel_p = platform::from_utf8(p).parent_path();
    while (!rel_p.empty() && rel_p != ".") {
      std::string dir_str = platform::to_utf8(rel_p);
      std::error_code ec;
      if (!std::filesystem::exists(engine_.config().root / rel_p, ec)) {
        if (std::find(manifest_.created_dirs.begin(), manifest_.created_dirs.end(), dir_str) == manifest_.created_dirs.end()) {
          manifest_.created_dirs.push_back(dir_str);
          dirty = true;
        }
      }
      rel_p = rel_p.parent_path();
    }
  }

  if (dirty) {
    save_manifest();
  }
}

void Changesets::after_write(const std::vector<std::pair<std::string, std::string>>& path_hashes) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!active_.has_value() || active_->status != "OPEN") return;

  for (const auto& [p, h] : path_hashes) {
    manifest_.files[p].last_written_hash = h;
    if (std::find(active_->files.begin(), active_->files.end(), p) == active_->files.end()) {
      active_->files.push_back(p);
    }
  }

  active_->updated_at = core::now_ms();
  save_manifest();
}

ChangesetState Changesets::recompute() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!active_.has_value() || active_->status != "OPEN") {
    return active_.value_or(ChangesetState{});
  }

  // Construct working set Buffer map for cumulative diff
  std::map<std::string, Buffer> ws;
  for (const auto& f : active_->files) {
    Buffer buf;
    buf.lang = lang::LanguageRegistry::get().for_path(f).lang;

    auto it_m = manifest_.files.find(f);
    bool existed = (it_m != manifest_.files.end()) && it_m->second.existed;

    if (existed) {
      std::filesystem::path bpath = base_file_path(manifest_.id, f);
      auto base_text = read_file_string(bpath);
      if (base_text.has_value()) {
        buf.pre = *base_text;
        buf.pre_facts = lang::extract_any(f, *buf.pre, buf.lang);
      }
    } else {
      buf.is_new = true;
    }

    std::filesystem::path full_p = engine_.config().root / platform::from_utf8(f);
    std::error_code ec;
    if (std::filesystem::exists(full_p, ec)) {
      auto curr_text = read_file_string(full_p);
      if (curr_text.has_value()) {
        buf.post = *curr_text;
        buf.post_facts = lang::extract_any(f, *buf.post, buf.lang);
      }
    } else {
      buf.is_deleted = true;
    }

    ws[f] = std::move(buf);
  }

  std::vector<Violation> violations = engine_.gate().audit_cumulative(ws);

  std::vector<Obligation> new_obligations;
  for (auto& v : violations) {
    if (is_deferrable_code(v.code)) {
      std::string f = v.location.has_value() ? v.location->file : "";
      std::string sym = v.symbol_id.value_or("");
      std::string ev_key = v.evidence.is_object() ? v.evidence.dump() : "";
      std::string oid = core::xxh3_hex(v.code + "|" + f + "|" + sym + "|" + ev_key).substr(0, 16);
      new_obligations.push_back(Obligation{oid, std::move(v)});
    }
  }

  // Sort obligations deterministically
  std::sort(new_obligations.begin(), new_obligations.end(), [](const Obligation& a, const Obligation& b) {
    std::string f_a = a.violation.location.has_value() ? a.violation.location->file : "";
    std::string f_b = b.violation.location.has_value() ? b.violation.location->file : "";
    if (f_a != f_b) return f_a < f_b;
    int l_a = a.violation.location.has_value() ? a.violation.location->line : 0;
    int l_b = b.violation.location.has_value() ? b.violation.location->line : 0;
    return l_a < l_b;
  });

  std::vector<std::string> discharged;
  for (const auto& old_ob : active_->obligations) {
    bool still_present = false;
    for (const auto& cur_ob : new_obligations) {
      if (cur_ob.id == old_ob.id) {
        still_present = true;
        break;
      }
    }
    if (!still_present) {
      discharged.push_back(old_ob.id);
    }
  }

  active_->obligations = std::move(new_obligations);
  active_->discharged = std::move(discharged);
  int64_t now = core::now_ms();
  active_->updated_at = now;

  // Sync with FalkorDB
  if (engine_.client().ping()) {
    try {
      engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "obligations_clear",
          .cypher = db::Q_OBLIGATIONS_CLEAR,
          .params = {{"id", active_->id}},
          .read_only = false
      });

      if (!active_->obligations.empty()) {
        std::vector<nlohmann::json> rows;
        for (const auto& ob : active_->obligations) {
          rows.push_back({
              {"oid", ob.id},
              {"code", ob.violation.code},
              {"message", ob.violation.message},
              {"fix_hint", ob.violation.fix_hint},
              {"file", ob.violation.location.has_value() ? ob.violation.location->file : ""},
              {"line", ob.violation.location.has_value() ? ob.violation.location->line : 0},
              {"col", ob.violation.location.has_value() ? ob.violation.location->col : 0},
              {"symbol_id", ob.violation.symbol_id.value_or("")}
          });
        }

        engine_.client().rows(engine_.config().project_graph(), db::Query{
            .name = "obligations_create",
            .cypher = db::Q_OBLIGATIONS_CREATE,
            .params = {
                {"id", active_->id},
                {"rows", rows},
                {"now", now}
            },
            .read_only = false
        });
      }

      engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "changeset_touch",
          .cypher = db::Q_CHANGESET_TOUCH,
          .params = {
              {"id", active_->id},
              {"files", active_->files},
              {"open_obligations", static_cast<int64_t>(active_->obligations.size())},
              {"now", now},
              {"session", engine_.sessions().ctx().session_id}
          },
          .read_only = false
      });
    } catch (...) {}
  }

  save_manifest();
  return *active_;
}

ChangesetState Changesets::finalize(const memory::SessionCtx& session, bool force) {
  (void)session;
  std::lock_guard<std::mutex> lock(mu_);
  if (!active_.has_value() || active_->status != "OPEN") {
    throw mcp::ToolError("NO_OPEN_CHANGESET", "No changeset is currently open");
  }

  mu_.unlock();
  recompute();
  mu_.lock();

  if (!active_->obligations.empty() && !force) {
    return *active_;
  }

  int64_t now = core::now_ms();
  std::string id = active_->id;

  if (force && !active_->obligations.empty() && engine_.client().ping()) {
    // Record forced open obligations as Pitfalls per §6.14.2
    for (const auto& ob : active_->obligations) {
      try {
        std::string sid = ob.violation.symbol_id.value_or("");
        std::string f = ob.violation.location.has_value() ? ob.violation.location->file : "";
        std::string pkey = core::xxh3_hex("OBLIGATION_FORCED_OPEN|" + (sid.empty() ? f : sid));
        engine_.client().rows(engine_.config().project_graph(), db::Query{
            .name = "pitfall_upsert",
            .cypher = Q_FORCED_PITFALL,
            .params = {
                {"key", pkey},
                {"code", "OBLIGATION_FORCED_OPEN"},
                {"symbol_id", sid},
                {"file", f},
                {"lesson", "Changeset forced closed with unresolved obligation: " + ob.violation.message},
                {"rule_id", ""},
                {"now", now}
            },
            .read_only = false
        });
      } catch (...) {}
    }
  }

  if (engine_.client().ping()) {
    try {
      engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "changeset_set_status",
          .cypher = db::Q_CHANGESET_SET_STATUS,
          .params = {
              {"id", id},
              {"status", "CLOSED"},
              {"now", now}
          },
          .read_only = false
      });
    } catch (...) {}
  }

  // Remove snapshot directory
  std::error_code ec;
  std::filesystem::remove_all(changeset_dir(id), ec);

  ChangesetState res = *active_;
  res.status = "CLOSED";
  res.updated_at = now;
  active_.reset();
  manifest_ = ChangesetManifest{};
  return res;
}

ChangesetState Changesets::abandon(const memory::SessionCtx& session, bool force) {
  (void)session;
  std::lock_guard<std::mutex> lock(mu_);
  if (!active_.has_value() || active_->status != "OPEN") {
    throw mcp::ToolError("NO_OPEN_CHANGESET", "No changeset is currently open");
  }

  // Conflict detection: verify current disk contents match last_written_hash
  std::vector<std::string> conflicts;
  for (const auto& f : active_->files) {
    std::filesystem::path full_p = engine_.config().root / platform::from_utf8(f);
    std::string curr_hash;
    std::error_code ec;
    if (std::filesystem::exists(full_p, ec)) {
      auto content = read_file_string(full_p);
      if (content.has_value()) {
        curr_hash = core::xxh3_hex(*content);
      }
    }
    auto it_m = manifest_.files.find(f);
    if (it_m != manifest_.files.end()) {
      if (curr_hash != it_m->second.last_written_hash) {
        conflicts.push_back(f);
      }
    }
  }

  if (!conflicts.empty() && !force) {
    ChangesetState conflict_res = *active_;
    conflict_res.conflicts = conflicts;
    conflict_res.message = "files changed outside the changeset; resolve or ask the user to run graphstate changeset abandon --force";
    return conflict_res;
  }

  // Restore base files
  for (const auto& f : active_->files) {
    std::filesystem::path full_p = engine_.config().root / platform::from_utf8(f);
    auto it_m = manifest_.files.find(f);
    if (it_m != manifest_.files.end() && it_m->second.existed) {
      std::filesystem::path bpath = base_file_path(manifest_.id, f);
      auto base_text = read_file_string(bpath);
      if (base_text.has_value()) {
        platform::write_file_durable(full_p, *base_text);
      }
    } else {
      platform::remove_file(full_p);
    }
  }

  // Remove empty created directories in reverse length order
  std::sort(manifest_.created_dirs.begin(), manifest_.created_dirs.end(), [](const std::string& a, const std::string& b) {
    return a.size() > b.size();
  });
  for (const auto& d : manifest_.created_dirs) {
    std::error_code ec;
    std::filesystem::path dir_p = engine_.config().root / platform::from_utf8(d);
    if (std::filesystem::exists(dir_p, ec) && std::filesystem::is_empty(dir_p, ec)) {
      std::filesystem::remove(dir_p, ec);
    }
  }

  int64_t now = core::now_ms();
  std::string id = active_->id;

  if (engine_.client().ping()) {
    try {
      engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "changeset_set_status",
          .cypher = db::Q_CHANGESET_SET_STATUS,
          .params = {
              {"id", id},
              {"status", "ABANDONED"},
              {"now", now}
          },
          .read_only = false
      });
    } catch (...) {}
  }

  // Delete snapshot directory
  std::error_code ec;
  std::filesystem::remove_all(changeset_dir(id), ec);

  ChangesetState res = *active_;
  res.status = "ABANDONED";
  res.updated_at = now;
  active_.reset();
  manifest_ = ChangesetManifest{};
  return res;
}

} // namespace gs::gate
