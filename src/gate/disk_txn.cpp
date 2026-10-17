#include "gate/disk_txn.hpp"

#include <chrono>
#include <fstream>
#include <system_error>
#include "core/clock.hpp"
#include "core/error.hpp"
#include "db/queries.hpp"
#include "platform/platform.hpp"

namespace gs::gate {

DiskTxn::DiskTxn(std::filesystem::path root, std::string patch_id, bool durable)
    : root_(std::move(root)),
      patch_id_(std::move(patch_id)),
      wal_dir_(root_ / ".graphstate" / "wal"),
      backup_dir_(wal_dir_ / patch_id_),
      journal_path_(wal_dir_ / (patch_id_ + ".json")),
      durable_(durable) {
  state_.patch_id = patch_id_;
}

DiskTxn::~DiskTxn() {
  if (active_ && !committed_) {
    try {
      rollback();
    } catch (...) {
      // Suppress exceptions in destructor
    }
  }
}

void DiskTxn::write_journal(std::string_view state) {
  state_.state = std::string(state);
  nlohmann::json files_arr = nlohmann::json::array();
  for (const auto& f : state_.files) {
    files_arr.push_back({
        {"path", f.path},
        {"op", f.op},
        {"tmp_path", f.tmp_path},
        {"backup_path", f.backup_path},
        {"pre_hash", f.pre_hash},
        {"post_hash", f.post_hash},
        {"to_path", f.to_path}
    });
  }

  nlohmann::json j = {
      {"patch_id", state_.patch_id},
      {"state", state_.state},
      {"files", std::move(files_arr)},
      {"created_dirs", state_.created_dirs},
      {"decision_payload", state_.decision_payload},
      {"created_at", state_.created_at}
  };

  std::string content = j.dump(2);
  std::filesystem::path tmp_journal = wal_dir_ / (patch_id_ + ".tmp");
  platform::write_file_durable(tmp_journal, content, durable_);
  platform::replace_file(tmp_journal, journal_path_, durable_);
  if (durable_) {
    platform::fsync_dir(wal_dir_);
  }
}

void DiskTxn::prepare(const std::vector<TxnFile>& files,
                      const std::vector<std::string>& created_dirs,
                      const std::map<std::string, std::string>& post_contents,
                      const nlohmann::json& decision_payload) {
  std::error_code ec;
  std::filesystem::create_directories(wal_dir_, ec);
  std::filesystem::create_directories(backup_dir_, ec);

  state_.files = files;
  state_.created_dirs = created_dirs;
  state_.decision_payload = decision_payload;
  state_.created_at = core::now_ms();
  active_ = true;

  // 1. Back up existing files
  for (auto& f : state_.files) {
    if (f.op == "modified" || f.op == "deleted" || f.op == "moved") {
      std::filesystem::path src = root_ / platform::from_utf8(f.path);
      std::filesystem::path dst = backup_dir_ / platform::from_utf8(f.path);
      std::filesystem::create_directories(dst.parent_path(), ec);
      platform::hardlink_or_copy(src, dst);
      f.backup_path = platform::to_utf8(std::filesystem::relative(dst, wal_dir_));
    }
  }

  // 2. Write new / modified contents to .tmp files
  for (auto& f : state_.files) {
    if (f.op == "modified" || f.op == "created") {
      std::filesystem::path target = root_ / platform::from_utf8(f.path);
      std::filesystem::create_directories(target.parent_path(), ec);
      std::filesystem::path tmp = root_ / platform::from_utf8(f.path + ".gs-" + patch_id_ + ".tmp");
      f.tmp_path = platform::to_utf8(std::filesystem::relative(tmp, root_));

      auto it = post_contents.find(f.path);
      if (it != post_contents.end()) {
        platform::write_file_durable(tmp, it->second, durable_);

        // Preserve file permissions if file previously existed
        if (f.op == "modified") {
          platform::FileMode mode = platform::file_mode(target);
          platform::set_file_mode(tmp, mode);
        }
      }
    } else if (f.op == "moved") {
      std::filesystem::path target = root_ / platform::from_utf8(f.to_path);
      std::filesystem::create_directories(target.parent_path(), ec);
      std::filesystem::path tmp = root_ / platform::from_utf8(f.to_path + ".gs-" + patch_id_ + ".tmp");
      f.tmp_path = platform::to_utf8(std::filesystem::relative(tmp, root_));

      auto it = post_contents.find(f.to_path);
      if (it != post_contents.end()) {
        platform::write_file_durable(tmp, it->second, durable_);
      }
    }
  }

  // 3. Write journal in PREPARED state
  write_journal("PREPARED");
}

void DiskTxn::commit() {
  if (!active_) return;

  // Step 5: set state RENAMING
  write_journal("RENAMING");

  // Step 6: replace or remove files
  std::vector<std::filesystem::path> touched_dirs;
  for (const auto& f : state_.files) {
    if (f.op == "modified" || f.op == "created") {
      std::filesystem::path tmp = root_ / platform::from_utf8(f.tmp_path);
      std::filesystem::path dst = root_ / platform::from_utf8(f.path);
      platform::replace_file(tmp, dst, durable_);
      touched_dirs.push_back(dst.parent_path());
    } else if (f.op == "deleted") {
      std::filesystem::path dst = root_ / platform::from_utf8(f.path);
      platform::remove_file(dst);
      touched_dirs.push_back(dst.parent_path());
    } else if (f.op == "moved") {
      std::filesystem::path src = root_ / platform::from_utf8(f.path);
      std::filesystem::path tmp = root_ / platform::from_utf8(f.tmp_path);
      std::filesystem::path dst = root_ / platform::from_utf8(f.to_path);
      platform::replace_file(tmp, dst, durable_);
      platform::remove_file(src);
      touched_dirs.push_back(src.parent_path());
      touched_dirs.push_back(dst.parent_path());
    }
  }

  if (durable_) {
    for (const auto& d : touched_dirs) {
      platform::fsync_dir(d);
    }
  }

  // Step 7: set state RENAMED
  write_journal("RENAMED");
  committed_ = true;
}

void DiskTxn::rollback() {
  std::error_code ec;

  // If we were RENAMING, restore backups and delete created files
  for (const auto& f : state_.files) {
    if (f.op == "modified" || f.op == "deleted" || f.op == "moved") {
      std::filesystem::path backup = wal_dir_ / platform::from_utf8(f.backup_path);
      std::filesystem::path dst = root_ / platform::from_utf8(f.path);
      if (std::filesystem::exists(backup, ec)) {
        try {
          platform::replace_file(backup, dst);
        } catch (...) {}
      }
    }
    if (f.op == "created") {
      std::filesystem::path created = root_ / platform::from_utf8(f.path);
      platform::remove_file(created);
    }
    if (f.op == "moved" && !f.to_path.empty()) {
      std::filesystem::path created = root_ / platform::from_utf8(f.to_path);
      platform::remove_file(created);
    }

    // Clean up temporary files
    if (!f.tmp_path.empty()) {
      std::filesystem::path tmp = root_ / platform::from_utf8(f.tmp_path);
      platform::remove_file(tmp);
    }
  }

  // Remove empty created dirs in reverse
  for (auto it = state_.created_dirs.rbegin(); it != state_.created_dirs.rend(); ++it) {
    std::filesystem::path d = root_ / platform::from_utf8(*it);
    if (std::filesystem::is_empty(d, ec)) {
      std::filesystem::remove(d, ec);
    }
  }

  cleanup();
  active_ = false;
}

void DiskTxn::cleanup() {
  std::error_code ec;
  if (std::filesystem::exists(backup_dir_, ec)) {
    std::filesystem::remove_all(backup_dir_, ec);
  }
  if (std::filesystem::exists(journal_path_, ec)) {
    std::filesystem::remove(journal_path_, ec);
  }
}

void DiskTxn::recover_startup(const std::filesystem::path& root, db::GraphClient& client, const std::string& project_graph) {
  std::filesystem::path wal_dir = root / ".graphstate" / "wal";
  std::error_code ec;
  if (!std::filesystem::exists(wal_dir, ec)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(wal_dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      try {
        std::ifstream in(entry.path());
        if (!in.is_open()) continue;
        nlohmann::json j = nlohmann::json::parse(in);
        in.close();

        std::string patch_id = j.value("patch_id", "");
        std::string state = j.value("state", "");
        std::filesystem::path backup_dir = wal_dir / patch_id;

        if (state == "PREPARED") {
          // Clean up tmp files
          if (j.contains("files") && j["files"].is_array()) {
            for (const auto& f : j["files"]) {
              std::string tmp_rel = f.value("tmp_path", "");
              if (!tmp_rel.empty()) {
                platform::remove_file(root / platform::from_utf8(tmp_rel));
              }
            }
          }
          if (std::filesystem::exists(backup_dir, ec)) {
            std::filesystem::remove_all(backup_dir, ec);
          }
          if (j.contains("created_dirs") && j["created_dirs"].is_array()) {
            for (auto it = j["created_dirs"].rbegin(); it != j["created_dirs"].rend(); ++it) {
              std::filesystem::path d = root / platform::from_utf8(it->get<std::string>());
              if (std::filesystem::is_empty(d, ec)) {
                std::filesystem::remove(d, ec);
              }
            }
          }
        } else if (state == "RENAMING") {
          if (j.contains("files") && j["files"].is_array()) {
            for (const auto& f : j["files"]) {
              std::string op = f.value("op", "");
              std::string path = f.value("path", "");
              std::string to_path = f.value("to_path", "");
              std::string backup_rel = f.value("backup_path", "");
              std::string tmp_rel = f.value("tmp_path", "");

              if (!tmp_rel.empty()) {
                platform::remove_file(root / platform::from_utf8(tmp_rel));
              }

              if (op == "modified" || op == "deleted" || op == "moved") {
                if (!backup_rel.empty()) {
                  std::filesystem::path bkp = wal_dir / platform::from_utf8(backup_rel);
                  std::filesystem::path dst = root / platform::from_utf8(path);
                  if (std::filesystem::exists(bkp, ec)) {
                    platform::replace_file(bkp, dst);
                  }
                }
              }
              if (op == "created") {
                platform::remove_file(root / platform::from_utf8(path));
              }
              if (op == "moved" && !to_path.empty()) {
                platform::remove_file(root / platform::from_utf8(to_path));
              }
            }
          }
          if (std::filesystem::exists(backup_dir, ec)) {
            std::filesystem::remove_all(backup_dir, ec);
          }
        } else if (state == "RENAMED") {
          // Roll forward: record APPLIED Decision if client is connected
          if (client.ping() && !project_graph.empty() && j.contains("decision_payload") && j["decision_payload"].is_object()) {
            try {
              client.exec(project_graph, db::Query{
                  .name = "decision_record",
                  .cypher = db::Q_DECISION_RECORD,
                  .params = {
                      {"d", j["decision_payload"]},
                      {"session", ""},
                      {"intent_id", ""}
                  },
                  .read_only = false
              });
            } catch (...) {}
          }
          if (std::filesystem::exists(backup_dir, ec)) {
            std::filesystem::remove_all(backup_dir, ec);
          }
        }

        std::filesystem::remove(entry.path(), ec);
      } catch (...) {}
    }
  }
}

} // namespace gs::gate
