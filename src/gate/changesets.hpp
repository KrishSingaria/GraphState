#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "gate/patch.hpp"
#include "memory/session_ctx.hpp"

namespace gs {
class Engine;
}

namespace gs::gate {

struct Obligation {
  std::string id;          // xxh3_hex of code|file|symbol|evidence-key
  Violation violation;     // code ∈ deferrable set, with location / evidence / fix_hint
};

struct ChangesetState {
  std::string id;          // "c_" + 12 hex
  std::string intent;
  std::string status = "OPEN"; // OPEN | CLOSED | ABANDONED
  std::vector<std::string> files;          // touched so far
  std::vector<Obligation> obligations;     // current (after recompute)
  std::vector<std::string> discharged;     // obligation ids cleared since previous recompute
  std::vector<std::string> conflicts;      // files modified out-of-band during abandon
  std::string message;                     // info or warning message
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

struct ChangesetFileEntry {
  bool existed = false;
  std::string base_hash;
  std::string last_written_hash;
};

struct ChangesetManifest {
  std::string id;
  std::string intent;
  std::string status = "OPEN";
  int64_t created_at = 0;
  std::map<std::string, ChangesetFileEntry> files;
  std::vector<std::string> created_dirs;
};

nlohmann::json to_json(const Obligation& ob);
nlohmann::json to_json(const ChangesetState& cs);

class Changesets {
public:
  explicit Changesets(Engine& engine);
  ~Changesets();

  void load_on_startup();
  ChangesetState begin(std::string_view intent, const memory::SessionCtx& session);
  std::optional<ChangesetState> current() const;
  void before_write(const std::vector<std::string>& paths);
  void after_write(const std::vector<std::pair<std::string, std::string>>& path_hashes);
  ChangesetState recompute();
  ChangesetState finalize(const memory::SessionCtx& session, bool force = false);
  ChangesetState abandon(const memory::SessionCtx& session, bool force = false);

  static bool is_deferrable_code(std::string_view code);

private:
  Engine& engine_;
  mutable std::mutex mu_;
  std::optional<ChangesetState> active_;
  ChangesetManifest manifest_;

  std::filesystem::path changeset_dir(const std::string& id) const;
  std::filesystem::path manifest_path(const std::string& id) const;
  std::filesystem::path base_file_path(const std::string& id, const std::string& rel_file) const;
  void save_manifest();
  bool load_manifest(const std::string& id);
};

} // namespace gs::gate
