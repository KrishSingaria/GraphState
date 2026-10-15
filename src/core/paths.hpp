#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gs::core {

// ── Glob Matching ────────────────────────────────────────────────────────────
// Evaluates pattern against rel_path (both '/' separated).
// Supports '**', '*', '?', and '{a,b}' alternation.
bool glob_match(std::string_view pattern, std::string_view rel_path, bool case_insensitive = false);

// Expands comma-separated braces (e.g. "*.{py,ts}" -> ["*.py", "*.ts"]).
std::vector<std::string> expand_braces(std::string_view pattern);

// ── Path Normalization & Safety ──────────────────────────────────────────────
// Normalizes raw path into workspace-relative forward-slash separated path.
// Throws gs::Error(ErrorCode::PATH_OUTSIDE_WORKSPACE) if path escapes or is absolute.
// Throws gs::Error(ErrorCode::PATH_INVALID) if unportable or Win32 reserved name.
std::string normalize_rel_path(std::string_view raw_path);

// Validates that rel_path is safe: no reserved device names (CON, NUL, AUX, etc.),
// no trailing dot/space, no illegal characters (<>:"|?*), and no '..' segments.
bool is_safe_rel_path(std::string_view rel_path);
bool is_portable_filename(std::string_view rel_path);

// Converts absolute target into workspace-relative path against root.
// Throws gs::Error(ErrorCode::PATH_OUTSIDE_WORKSPACE) if target is outside root.
std::string to_workspace_rel(const std::filesystem::path& root, const std::filesystem::path& target);

// Checks if weakly_canonical(root / rel) is strictly contained within root.
// If file exists, also verifies canonical path does not escape via symlink/junction.
bool is_contained(const std::filesystem::path& root, std::string_view rel_path);

} // namespace gs::core
