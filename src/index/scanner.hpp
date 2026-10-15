#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "core/config.hpp"

namespace gs::index {

struct ScannedFile {
  std::string rel;       // Workspace-relative path, '/' separated, exact on-disk casing
  uint64_t size = 0;     // Size in bytes
  int64_t mtime_ns = 0;  // Modification timestamp in nanoseconds since Unix epoch

  bool operator==(const ScannedFile& o) const = default;
};

// Binary detection: returns true if the first 8,192 bytes contain a NUL ('\0') byte.
bool is_binary_file(const std::filesystem::path& abs_path);

// Workspace discovery: dual-path scan (Git fast path -> Filesystem fallback)
std::vector<ScannedFile> scan(const core::Config& config, const std::filesystem::path& root);

// Convenience overload using config.root
inline std::vector<ScannedFile> scan_workspace(const core::Config& config) {
  return scan(config, config.root);
}

} // namespace gs::index
