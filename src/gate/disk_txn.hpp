#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "db/graph_client.hpp"

namespace gs::gate {

struct TxnFile {
  std::string path;         // relative path, '/' separated
  std::string op;           // "modified", "created", "deleted", "moved"
  std::string tmp_path;     // relative path to .tmp file
  std::string backup_path;  // relative path inside wal directory
  std::string pre_hash;
  std::string post_hash;
  std::string to_path;      // for moved
};

struct TxnState {
  std::string patch_id;
  std::string state; // "PREPARED", "RENAMING", "RENAMED"
  std::vector<TxnFile> files;
  std::vector<std::string> created_dirs;
  nlohmann::json decision_payload;
  int64_t created_at = 0;
};

class DiskTxn {
public:
  DiskTxn(std::filesystem::path root, std::string patch_id, bool durable = true);
  ~DiskTxn();

  DiskTxn(const DiskTxn&) = delete;
  DiskTxn& operator=(const DiskTxn&) = delete;

  // Step 1 - 4: backup, write durable tmps, write journal with PREPARED
  void prepare(const std::vector<TxnFile>& files,
               const std::vector<std::string>& created_dirs,
               const std::map<std::string, std::string>& post_contents,
               const nlohmann::json& decision_payload);

  // Step 5 - 7: state RENAMING, replace/remove files, state RENAMED
  void commit();

  // Explicit rollback if anything fails during prepare/commit
  void rollback();

  // Step 10: remove WAL journal and backup dir
  void cleanup();

  // Recovery run at startup for any leftover W/*.json
  static void recover_startup(const std::filesystem::path& root, db::GraphClient& client, const std::string& project_graph = "");

private:
  void write_journal(std::string_view state);

  std::filesystem::path root_;
  std::string patch_id_;
  std::filesystem::path wal_dir_;
  std::filesystem::path backup_dir_;
  std::filesystem::path journal_path_;
  TxnState state_;
  bool committed_ = false;
  bool active_ = false;
  bool durable_ = true;
};

} // namespace gs::gate
