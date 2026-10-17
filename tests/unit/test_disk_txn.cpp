#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "db/graph_client.hpp"
#include "gate/disk_txn.hpp"

using namespace gs;
using namespace gs::gate;

namespace {

struct TempDir {
  std::filesystem::path root;

  TempDir() {
    root = std::filesystem::temp_directory_path() / ("gs_test_disktxn_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  void write_file(const std::string& rel, const std::string& content) {
    std::filesystem::path p = root / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << content;
  }

  std::string read_file(const std::string& rel) {
    std::filesystem::path p = root / rel;
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
};

} // namespace

TEST_CASE("DiskTxn: WAL and atomic write transactions") {
  TempDir dir;
  dir.write_file("file1.txt", "initial content 1\n");
  dir.write_file("file2.txt", "initial content 2\n");

  SUBCASE("Normal commit flow") {
    DiskTxn txn(dir.root, "p_test01");
    std::vector<TxnFile> files = {
        {.path = "file1.txt", .op = "modified", .pre_hash = "h1", .post_hash = "h1_new"},
        {.path = "file3.txt", .op = "created", .pre_hash = "", .post_hash = "h3_new"}
    };
    std::map<std::string, std::string> post_contents = {
        {"file1.txt", "updated content 1\n"},
        {"file3.txt", "created content 3\n"}
    };

    txn.prepare(files, {}, post_contents, {});

    // Verify journal exists during transaction
    CHECK(std::filesystem::exists(dir.root / ".graphstate" / "wal" / "p_test01.json"));
    // Verify backup exists
    CHECK(std::filesystem::exists(dir.root / ".graphstate" / "wal" / "p_test01" / "file1.txt"));

    txn.commit();
    txn.cleanup();

    // Verify files were committed to disk
    CHECK(dir.read_file("file1.txt") == "updated content 1\n");
    CHECK(dir.read_file("file3.txt") == "created content 3\n");

    // Verify journal and backup removed
    CHECK(!std::filesystem::exists(dir.root / ".graphstate" / "wal" / "p_test01.json"));
    CHECK(!std::filesystem::exists(dir.root / ".graphstate" / "wal" / "p_test01"));
  }

  SUBCASE("Rollback restores original files") {
    DiskTxn txn(dir.root, "p_test02");
    std::vector<TxnFile> files = {
        {.path = "file1.txt", .op = "modified", .pre_hash = "h1", .post_hash = "h1_new"},
        {.path = "new_file.txt", .op = "created", .pre_hash = "", .post_hash = "h_new"}
    };
    std::map<std::string, std::string> post_contents = {
        {"file1.txt", "failed edit content\n"},
        {"new_file.txt", "should not persist\n"}
    };

    txn.prepare(files, {}, post_contents, {});
    // Simulate failure before commit
    txn.rollback();

    CHECK(dir.read_file("file1.txt") == "initial content 1\n");
    CHECK(!std::filesystem::exists(dir.root / "new_file.txt"));
    CHECK(!std::filesystem::exists(dir.root / ".graphstate" / "wal" / "p_test02.json"));
  }

  SUBCASE("recover_startup handles orphaned RENAMING state") {
    // Manually create a journal simulating crash during RENAMING
    std::filesystem::path wal_dir = dir.root / ".graphstate" / "wal";
    std::filesystem::create_directories(wal_dir / "p_crash01");

    dir.write_file(".graphstate/wal/p_crash01/file1.txt", "restored content 1\n");
    dir.write_file("file1.txt", "corrupted in-flight content\n");

    nlohmann::json journal = {
        {"patch_id", "p_crash01"},
        {"state", "RENAMING"},
        {"files", nlohmann::json::array({
            {
                {"path", "file1.txt"},
                {"op", "modified"},
                {"backup_path", "p_crash01/file1.txt"},
                {"tmp_path", "file1.txt.gs-p_crash01.tmp"}
            }
        })}
    };
    dir.write_file(".graphstate/wal/p_crash01.json", journal.dump());

    db::GraphClient mock_client("127.0.0.1", 9999); // disconnected
    DiskTxn::recover_startup(dir.root, mock_client);

    // Should have restored backup
    CHECK(dir.read_file("file1.txt") == "restored content 1\n");
    // Journal should be cleaned up
    CHECK(!std::filesystem::exists(wal_dir / "p_crash01.json"));
  }
}
