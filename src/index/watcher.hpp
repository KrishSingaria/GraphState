#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gs {
class Engine;
}

namespace gs::index {

class Watcher {
public:
  explicit Watcher(Engine& engine);
  ~Watcher();

  Watcher(const Watcher&) = delete;
  Watcher& operator=(const Watcher&) = delete;

  void start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Execute a single check tick synchronously (for testing or immediate sync)
  void tick();

private:
  void loop();

  Engine& engine_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::mutex cv_mu_;
  std::condition_variable cv_;

  int64_t last_rules_mtime_{0};
  int64_t last_git_head_mtime_{0};
  int64_t last_git_index_mtime_{0};
  int64_t last_rescan_ts_{0};

  // Files checked within 2 seconds of their mtime that need re-check on next tick (racy-clean rule §12.8)
  std::unordered_set<std::string> racy_clean_files_;
};

} // namespace gs::index
