#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace gs::gate {

enum class OpKind {
  ReplaceSymbol,
  InsertSymbol,
  DeleteSymbol,
  ReplaceText,
  CreateFile,
  DeleteFile,
  MoveFile
};

std::string_view op_kind_name(OpKind op);
std::optional<OpKind> parse_op_kind(std::string_view s);

struct InsertAnchor {
  std::optional<std::string> after;
  std::optional<std::string> before;
  std::optional<std::string> into_class;
  bool at_start = false;
};

struct Edit {
  OpKind op = OpKind::ReplaceText;
  std::string file;                       // workspace-relative, '/' separators
  std::optional<std::string> symbol;      // ReplaceSymbol/DeleteSymbol: qualname or full id
  std::optional<std::string> code;        // ReplaceSymbol/InsertSymbol: full definition text
  std::optional<InsertAnchor> anchor;     // InsertSymbol
  std::optional<std::string> old_text;    // ReplaceText: must occur exactly once in current buffer
  std::optional<std::string> new_text;    // ReplaceText
  std::optional<std::string> content;     // CreateFile
  std::optional<std::string> to;          // MoveFile: destination path
};

struct PatchRequest {
  std::string intent;                     // 1..500 chars, required
  std::string reasoning;                  // 1..4000 chars, required
  std::vector<Edit> edits;                // 1..200
  std::map<std::string, std::string> base_hashes; // optional: file -> hash the agent last saw
  bool dry_run = false;
};

enum class Severity {
  Error,
  Warn,
  Info
};

std::string_view severity_name(Severity s);

struct Location {
  std::string file;
  int line = 0;
  int col = 0; // 1-based; col in UTF-8 bytes + 1
};

struct Violation {
  std::string code;                       // see §6.10 catalogue
  Severity severity = Severity::Error;
  std::string phase;                      // "PREFLIGHT"|"EDIT"|"SYNTAX"|"BLAST_RADIUS"|"REFERENCES"|"QUALITY"|"WRITE"
  std::string message;                    // one sentence, imperative fix where possible
  std::optional<std::string> symbol_id;   // the symbol the violation is ABOUT (callee for caller breaks)
  std::optional<Location> location;       // where to look (call site, syntax error, rule hit)
  nlohmann::json evidence = nlohmann::json::object(); // code-specific, see §6.10
  std::string fix_hint;                   // concrete suggestion
  std::optional<std::string> rule_id;     // for RULE_VIOLATION
};

enum class PatchStatus {
  Applied,
  Rejected,
  Validated /*dry_run ok*/
};

std::string_view patch_status_name(PatchStatus s);

struct FileChange {
  std::string file;
  std::string change; // "modified"|"created"|"deleted"|"moved"
  std::string pre_hash;
  std::string post_hash;
  std::vector<std::string> symbols_added;
  std::vector<std::string> symbols_removed;
  std::vector<std::string> symbols_signature_changed;
  std::vector<std::string> symbols_body_changed;
};

struct PatchResult {
  std::string patch_id;                   // "p_" + 12 hex
  PatchStatus status = PatchStatus::Rejected;
  std::optional<std::string> failed_phase;
  std::vector<Violation> violations;      // severity Error
  std::vector<Violation> warnings;        // severity Warn/Info
  std::vector<FileChange> changes;        // post-state diff summary
  std::vector<nlohmann::json> lessons;    // past pitfalls relevant to touched symbols
  std::map<std::string, double> timings_ms; // "p0".."p6","total"
  std::optional<std::string> suggestion;
  std::optional<nlohmann::json> changeset;
};

nlohmann::json to_json(const Location& loc);
nlohmann::json to_json(const Violation& v);
nlohmann::json to_json(const FileChange& fc);
nlohmann::json to_json(const PatchResult& r);
PatchRequest parse_patch_request(const nlohmann::json& j);

} // namespace gs::gate
