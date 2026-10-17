#include "gate/gate.hpp"

#include <chrono>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include "core/config.hpp"
#include "core/engine.hpp"
#include "core/error.hpp"
#include "core/hash.hpp"
#include "core/paths.hpp"
#include "core/text.hpp"
#include "core/clock.hpp"
#include "db/queries.hpp"
#include "gate/changesets.hpp"
#include "gate/disk_txn.hpp"
#include "lang/binding.hpp"
#include "lang/extractor.hpp"
#include "lang/language.hpp"
#include "lang/parser.hpp"
#include "platform/platform.hpp"

namespace gs::gate {

namespace {

std::string make_patch_id(std::string_view intent, std::string_view reasoning) {
  std::string seed = std::string(intent) + "|" + std::string(reasoning) + "|" + std::to_string(core::now_ms());
  return "p_" + core::xxh3_hex(seed).substr(0, 12);
}

std::string format_did_you_mean(const std::vector<std::string>& candidates) {
  std::string out = "[";
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (i > 0) out += ", ";
    out += "\"" + candidates[i] + "\"";
  }
  out += "]";
  return out;
}

std::vector<std::string> find_similar_names(std::string_view target,
                                            const std::vector<std::string>& pool,
                                            size_t max_results = 3) {
  std::vector<std::pair<int, std::string>> scored;
  int target_len = static_cast<int>(target.size());
  int max_dist = std::max(2, target_len / 3);

  for (const auto& candidate : pool) {
    if (candidate == target) continue;
    int dist = static_cast<int>(core::damerau_levenshtein(target, candidate));
    bool prefix_match = (target.size() >= 4 && candidate.size() >= 4 &&
                         target.substr(0, 4) == candidate.substr(0, 4));
    if (dist <= max_dist || prefix_match) {
      scored.push_back({dist, candidate});
    }
  }

  std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });

  std::vector<std::string> results;
  for (size_t i = 0; i < scored.size() && i < max_results; ++i) {
    results.push_back(scored[i].second);
  }
  return results;
}

std::string get_indent_string(std::string_view src, uint32_t start_byte) {
  if (start_byte > src.size()) return "";
  size_t line_start = src.rfind('\n', start_byte == 0 ? 0 : start_byte - 1);
  line_start = (line_start == std::string_view::npos) ? 0 : line_start + 1;
  std::string indent;
  for (size_t i = line_start; i < start_byte && i < src.size(); ++i) {
    if (src[i] == ' ' || src[i] == '\t') {
      indent += src[i];
    } else {
      break;
    }
  }
  return indent;
}

std::string ensure_eol(std::string_view src, bool is_crlf) {
  std::string out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size(); ++i) {
    if (src[i] == '\r') {
      continue;
    }
    if (src[i] == '\n') {
      if (is_crlf) {
        out += "\r\n";
      } else {
        out += '\n';
      }
    } else {
      out += src[i];
    }
  }
  return out;
}

std::string get_line_snippet(std::string_view text, int line_1based) {
  int current_line = 1;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (current_line == line_1based) {
      size_t end = text.find('\n', start);
      if (end == std::string_view::npos) end = text.size();
      if (end > start && text[end - 1] == '\r') --end;
      return std::string(text.substr(start, end - start));
    }
    if (text[i] == '\n') {
      ++current_line;
      start = i + 1;
    }
  }
  if (current_line == line_1based && start < text.size()) {
    return std::string(text.substr(start));
  }
  return "";
}

} // namespace

Gate::Gate(Engine& engine)
    : engine_(engine),
      rules_engine_(engine.config().root) {}

Gate::~Gate() = default;

PatchResult Gate::propose(const PatchRequest& req, const memory::SessionCtx& session) {
  auto t_start = std::chrono::steady_clock::now();
  PatchResult result;
  result.patch_id = make_patch_id(req.intent, req.reasoning);

  std::map<std::string, Buffer> ws;
  std::vector<Violation> violations;
  std::vector<Violation> warnings;
  std::vector<FileChange> changes;
  std::vector<std::string> targets;

  // Phase 0: Preflight
  auto t0 = std::chrono::steady_clock::now();
  p0_preflight(req, ws, violations, warnings);
  auto t1 = std::chrono::steady_clock::now();
  result.timings_ms["p0"] = std::chrono::duration<double, std::milli>(t1 - t0).count();

  if (!violations.empty()) {
    result.status = PatchStatus::Rejected;
    result.failed_phase = "PREFLIGHT";
    result.violations = std::move(violations);
    result.warnings = std::move(warnings);
    result.timings_ms["total"] = std::chrono::duration<double, std::milli>(t1 - t_start).count();
    return result;
  }

  // Phase 1: Edit
  p1_edit(req, ws, violations);
  auto t2 = std::chrono::steady_clock::now();
  result.timings_ms["p1"] = std::chrono::duration<double, std::milli>(t2 - t1).count();

  if (!violations.empty()) {
    result.status = PatchStatus::Rejected;
    result.failed_phase = "EDIT";
    result.violations = std::move(violations);
    result.warnings = std::move(warnings);
    result.timings_ms["total"] = std::chrono::duration<double, std::milli>(t2 - t_start).count();
    return result;
  }

  // Phase 2: Syntax Gate
  p2_syntax(ws, violations, warnings);
  auto t3 = std::chrono::steady_clock::now();
  result.timings_ms["p2"] = std::chrono::duration<double, std::milli>(t3 - t2).count();

  if (!violations.empty()) {
    result.status = PatchStatus::Rejected;
    result.failed_phase = "SYNTAX";
    result.violations = std::move(violations);
    result.warnings = std::move(warnings);
    result.timings_ms["total"] = std::chrono::duration<double, std::milli>(t3 - t_start).count();
    record_rejected(result.patch_id, req, session, "SYNTAX", result.violations, ws);
    return result;
  }

  // Phase 3: Symbol Diff
  p3_symbol_diff(ws, changes, targets, warnings);
  auto t4 = std::chrono::steady_clock::now();
  result.timings_ms["p3"] = std::chrono::duration<double, std::milli>(t4 - t3).count();

  // Phase 4 & Phase 5: Both run to give comprehensive feedback
  p4_blast_radius(ws, targets, violations, warnings);
  auto t5 = std::chrono::steady_clock::now();
  result.timings_ms["p4"] = std::chrono::duration<double, std::milli>(t5 - t4).count();

  p5_quality_rules(ws, violations, warnings);
  auto t6 = std::chrono::steady_clock::now();
  result.timings_ms["p5"] = std::chrono::duration<double, std::milli>(t6 - t5).count();

  result.lessons = fetch_lessons(targets, ws);
  result.changes = changes;
  result.warnings = warnings;

  auto cs_opt = engine_.changesets().current();
  bool in_changeset = cs_opt.has_value() && cs_opt->status == "OPEN";

  std::vector<Violation> blocking_violations;
  std::vector<Violation> deferred_violations;

  if (in_changeset) {
    for (auto& v : violations) {
      if (Changesets::is_deferrable_code(v.code)) {
        deferred_violations.push_back(std::move(v));
      } else {
        blocking_violations.push_back(std::move(v));
      }
    }
  } else {
    blocking_violations = std::move(violations);
  }

  // Check for blocking errors
  if (!blocking_violations.empty()) {
    result.status = PatchStatus::Rejected;
    result.failed_phase = "BLAST_RADIUS";
    for (const auto& v : blocking_violations) {
      if (v.phase == "QUALITY") {
        result.failed_phase = "QUALITY";
        break;
      }
    }
    result.violations = std::move(blocking_violations);

    if (!in_changeset) {
      bool all_deferrable = true;
      for (const auto& v : result.violations) {
        if (!Changesets::is_deferrable_code(v.code)) {
          all_deferrable = false;
          break;
        }
      }
      if (all_deferrable) {
        result.suggestion = "This looks like incremental building. Open a changeset (changeset {action:'begin', intent}) to apply now and track these as obligations, or include the missing pieces in this patch.";
      }
    }

    record_rejected(result.patch_id, req, session, *result.failed_phase, result.violations, ws);
    auto t_end = std::chrono::steady_clock::now();
    result.timings_ms["total"] = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
  }

  // Dry run check
  if (req.dry_run) {
    result.status = PatchStatus::Validated;
    auto t_end = std::chrono::steady_clock::now();
    result.timings_ms["total"] = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
  }

  // Phase 6: Write
  auto t6_start = std::chrono::steady_clock::now();
  try {
    if (in_changeset) {
      std::vector<std::string> touched_paths;
      for (const auto& [f, _] : ws) touched_paths.push_back(f);
      engine_.changesets().before_write(touched_paths);
    }

    p6_write(result.patch_id, req, session, ws, changes);
    result.status = PatchStatus::Applied;

    if (in_changeset) {
      std::vector<std::pair<std::string, std::string>> path_hashes;
      for (const auto& [f, buf] : ws) {
        std::string post_h = "";
        if (buf.post.has_value() && !buf.is_deleted) {
          post_h = core::xxh3_hex(*buf.post);
        }
        path_hashes.push_back({f, post_h});
      }
      engine_.changesets().after_write(path_hashes);

      if (engine_.client().ping()) {
        try {
          engine_.client().rows(engine_.config().project_graph(), db::Query{
              .name = "changeset_link_decision",
              .cypher = db::Q_CHANGESET_LINK_DECISION,
              .params = {
                  {"id", cs_opt->id},
                  {"patch_id", result.patch_id}
              },
              .read_only = false
          });
        } catch (...) {}
      }

      ChangesetState cs_state = engine_.changesets().recompute();
      nlohmann::json cs_json = {
          {"id", cs_state.id},
          {"intent", cs_state.intent},
          {"status", cs_state.status},
          {"files", cs_state.files},
          {"open_obligations", cs_state.obligations.size()}
      };
      nlohmann::json obs_arr = nlohmann::json::array();
      for (size_t i = 0; i < cs_state.obligations.size() && i < 20; ++i) {
        obs_arr.push_back(to_json(cs_state.obligations[i]));
      }
      cs_json["obligations"] = std::move(obs_arr);
      cs_json["discharged"] = cs_state.discharged;
      result.changeset = std::move(cs_json);
    }
  } catch (const gs::Error& err) {
    result.status = PatchStatus::Rejected;
    result.failed_phase = "WRITE";
    result.violations.push_back(Violation{
        .code = "WRITE_FAILED",
        .severity = Severity::Error,
        .phase = "WRITE",
        .message = err.what(),
        .fix_hint = "Ensure file permissions allow writing and no other process has locked files."
    });
  } catch (const std::exception& ex) {
    result.status = PatchStatus::Rejected;
    result.failed_phase = "WRITE";
    result.violations.push_back(Violation{
        .code = "WRITE_FAILED",
        .severity = Severity::Error,
        .phase = "WRITE",
        .message = ex.what(),
        .fix_hint = "Check filesystem status."
    });
  }

  auto t7 = std::chrono::steady_clock::now();
  result.timings_ms["p6"] = std::chrono::duration<double, std::milli>(t7 - t6_start).count();
  result.timings_ms["total"] = std::chrono::duration<double, std::milli>(t7 - t_start).count();

  return result;
}

void Gate::p0_preflight(const PatchRequest& req,
                        std::map<std::string, Buffer>& ws,
                        std::vector<Violation>& violations,
                        std::vector<Violation>& warnings) {
  const auto& cfg = engine_.config().gate;

  if (req.intent.empty() || req.intent.size() > 500) {
    violations.push_back(Violation{
        .code = "PATCH_INVALID",
        .phase = "PREFLIGHT",
        .message = "Intent must be between 1 and 500 characters."
    });
  }
  if (req.reasoning.empty() || req.reasoning.size() > 4000) {
    violations.push_back(Violation{
        .code = "PATCH_INVALID",
        .phase = "PREFLIGHT",
        .message = "Reasoning must be between 1 and 4000 characters."
    });
  }
  if (req.edits.empty() || req.edits.size() > static_cast<size_t>(cfg.max_edits)) {
    violations.push_back(Violation{
        .code = "PATCH_INVALID",
        .phase = "PREFLIGHT",
        .message = "Patch edits count must be between 1 and " + std::to_string(cfg.max_edits) + "."
    });
  }

  std::unordered_set<std::string> distinct_files;
  uint64_t total_payload = 0;

  for (const auto& e : req.edits) {
    distinct_files.insert(e.file);
    if (e.to.has_value()) distinct_files.insert(*e.to);

    uint64_t edit_bytes = 0;
    if (e.code.has_value()) edit_bytes += e.code->size();
    if (e.content.has_value()) edit_bytes += e.content->size();
    if (e.new_text.has_value()) edit_bytes += e.new_text->size();

    if (edit_bytes > cfg.max_code_bytes) {
      violations.push_back(Violation{
          .code = "PATCH_INVALID",
          .phase = "PREFLIGHT",
          .message = "Edit payload in '" + e.file + "' exceeds 1 MiB limit."
      });
    }
    total_payload += edit_bytes;

    // Per-op validation
    switch (e.op) {
      case OpKind::ReplaceSymbol:
        if (!e.symbol.has_value() || e.symbol->empty() || !e.code.has_value()) {
          violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "replace_symbol requires 'symbol' and 'code'."});
        }
        break;
      case OpKind::InsertSymbol:
        if (!e.code.has_value()) {
          violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "insert_symbol requires 'code'."});
        }
        break;
      case OpKind::DeleteSymbol:
        if (!e.symbol.has_value() || e.symbol->empty()) {
          violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "delete_symbol requires 'symbol'."});
        }
        break;
      case OpKind::ReplaceText:
        if (!e.old_text.has_value() || !e.new_text.has_value()) {
          violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "replace_text requires 'old_text' and 'new_text'."});
        }
        break;
      case OpKind::CreateFile:
        if (!e.content.has_value()) {
          violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "create_file requires 'content'."});
        }
        break;
      case OpKind::MoveFile:
        if (!e.to.has_value() || e.to->empty()) {
          violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "move_file requires 'to'."});
        }
        break;
      case OpKind::DeleteFile:
        break;
    }

    if (e.anchor.has_value()) {
      int count_anchors = (e.anchor->after ? 1 : 0) + (e.anchor->before ? 1 : 0) +
                          (e.anchor->into_class ? 1 : 0) + (e.anchor->at_start ? 1 : 0);
      if (count_anchors > 1) {
        violations.push_back(Violation{.code = "PATCH_INVALID", .phase = "PREFLIGHT", .message = "insert_symbol anchor can specify at most one of after, before, into_class, at_start."});
      }
    }
  }

  if (distinct_files.size() > static_cast<size_t>(cfg.max_files)) {
    violations.push_back(Violation{
        .code = "PATCH_INVALID",
        .phase = "PREFLIGHT",
        .message = "Distinct touched files exceed limit of " + std::to_string(cfg.max_files) + "."
    });
  }

  if (total_payload > cfg.max_patch_bytes) {
    violations.push_back(Violation{
        .code = "PATCH_INVALID",
        .phase = "PREFLIGHT",
        .message = "Total patch payload exceeds 8 MiB limit."
    });
  }

  // Path safety checks
  auto check_path_security = [&](const std::string& path_str, bool is_create) {
    if (!core::is_safe_rel_path(path_str) || !core::is_portable_filename(path_str)) {
      violations.push_back(Violation{
          .code = "PATH_INVALID",
          .phase = "PREFLIGHT",
          .message = "Path '" + path_str + "' contains invalid characters or reserved names."
      });
      return;
    }

    if (!core::is_contained(engine_.config().root, path_str)) {
      violations.push_back(Violation{
          .code = "PATH_OUTSIDE_WORKSPACE",
          .phase = "PREFLIGHT",
          .message = "Path '" + path_str + "' resolves outside the workspace."
      });
      return;
    }

    for (const auto& pat : cfg.protected_patterns) {
      if (core::glob_match(pat, path_str, false)) {
        violations.push_back(Violation{
            .code = "PATH_PROTECTED",
            .phase = "PREFLIGHT",
            .message = "Path '" + path_str + "' is protected (" + pat + ")."
        });
        return;
      }
    }

    if (!is_create) {
      for (const auto& ig : engine_.config().index.ignore) {
        if (core::glob_match(ig, path_str, false)) {
          violations.push_back(Violation{
              .code = "PATH_IGNORED",
              .phase = "PREFLIGHT",
              .message = "Path '" + path_str + "' matches ignore pattern (" + ig + ")."
          });
          return;
        }
      }
    }
  };

  for (const auto& e : req.edits) {
    check_path_security(e.file, e.op == OpKind::CreateFile);
    if (e.to.has_value()) {
      check_path_security(*e.to, true);
    }
  }

  if (!violations.empty()) return;

  // File existence & pre-content loading
  std::error_code ec;
  for (const auto& f : distinct_files) {
    std::filesystem::path full = engine_.config().root / platform::from_utf8(f);
    bool exists = std::filesystem::exists(full, ec);

    // Check op constraints
    for (const auto& e : req.edits) {
      if (e.file == f) {
        if (e.op == OpKind::CreateFile && exists) {
          violations.push_back(Violation{
              .code = "FILE_EXISTS",
              .phase = "PREFLIGHT",
              .message = "Cannot create file '" + f + "' because it already exists."
          });
        } else if (e.op != OpKind::CreateFile && !exists) {
          violations.push_back(Violation{
              .code = "FILE_NOT_FOUND",
              .phase = "PREFLIGHT",
              .message = "File '" + f + "' does not exist."
          });
        }
      }
      if (e.to.has_value() && *e.to == f) {
        if (exists) {
          violations.push_back(Violation{
              .code = "FILE_EXISTS",
              .phase = "PREFLIGHT",
              .message = "Move target '" + f + "' already exists."
          });
        }
      }
    }

    if (exists) {
      std::ifstream in(full, std::ios::binary);
      if (in.is_open()) {
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        Buffer b;
        b.pre = content;
        b.post = content;
        b.lang = lang::LanguageRegistry::get().for_path(f).lang;

        // Check base_hashes
        auto it_base = req.base_hashes.find(f);
        std::string disk_hash = core::hash_bytes(content);
        if (it_base != req.base_hashes.end() && it_base->second != disk_hash) {
          violations.push_back(Violation{
              .code = "STALE_BASE",
              .phase = "PREFLIGHT",
              .message = "File '" + f + "' changed since you last read it.",
              .evidence = {{"expected", it_base->second}, {"actual", disk_hash}},
              .fix_hint = "Re-read the symbol and rebuild the edit."
          });
        }

        // Check disk vs FactStore
        const auto* facts = engine_.fact_store().get(f);
        if (facts && facts->hash != disk_hash) {
          engine_.indexer().reindex({f});
          warnings.push_back(Violation{
              .code = "EXTERNAL_CHANGE_SYNCED",
              .severity = Severity::Info,
              .phase = "PREFLIGHT",
              .message = "External change to '" + f + "' synchronized.",
              .evidence = {{"path", f}}
          });
        }

        if (facts) {
          b.pre_facts = *facts;
        } else {
          b.pre_facts = lang::extract_any(f, content, b.lang);
        }

        ws[f] = std::move(b);
      }
    } else {
      Buffer b;
      b.lang = lang::LanguageRegistry::get().for_path(f).lang;
      b.is_new = true;
      ws[f] = std::move(b);
    }
  }

  // Semantic language support for symbol operations
  for (const auto& e : req.edits) {
    if (e.op == OpKind::ReplaceSymbol || e.op == OpKind::InsertSymbol || e.op == OpKind::DeleteSymbol) {
      const auto& linfo = lang::LanguageRegistry::get().for_path(e.file);
      if (!linfo.semantic) {
        violations.push_back(Violation{
            .code = "UNSUPPORTED_OP",
            .phase = "PREFLIGHT",
            .message = "Symbol operations not supported for language of '" + e.file + "'.",
            .fix_hint = "Use replace_text."
        });
      }
    }
  }
}

void Gate::p1_edit(const PatchRequest& req,
                   std::map<std::string, Buffer>& ws,
                   std::vector<Violation>& violations) {
  for (const auto& e : req.edits) {
    auto& buf = ws[e.file];

    if (e.op == OpKind::CreateFile) {
      buf.is_new = true;
      buf.is_deleted = false;
      std::string content = e.content.value_or("");
      if (!content.empty() && content.back() != '\n') {
        content += '\n';
      }
      buf.post = content;
      buf.edited_ranges.push_back({0, static_cast<uint32_t>(content.size())});
      continue;
    }

    if (e.op == OpKind::DeleteFile) {
      buf.is_deleted = true;
      buf.post = std::nullopt;
      continue;
    }

    if (e.op == OpKind::MoveFile) {
      buf.is_deleted = true;
      buf.moved_to = *e.to;
      auto& target_buf = ws[*e.to];
      target_buf.is_new = true;
      target_buf.is_deleted = false;
      target_buf.post = buf.post;
      target_buf.lang = lang::LanguageRegistry::get().for_path(*e.to).lang;
      buf.post = std::nullopt;
      continue;
    }

    if (!buf.post.has_value()) {
      continue;
    }

    std::string current_text = *buf.post;
    bool is_crlf = current_text.find("\r\n") != std::string::npos;

    if (e.op == OpKind::ReplaceText) {
      std::string old_s = *e.old_text;
      std::string new_s = *e.new_text;

      // Count occurrences
      size_t count = 0;
      size_t pos = 0;
      size_t found_pos = 0;
      std::vector<int> line_numbers;
      core::LineIndex line_idx(current_text);

      while ((pos = current_text.find(old_s, pos)) != std::string::npos) {
        ++count;
        found_pos = pos;
        line_numbers.push_back(static_cast<int>(line_idx.position_at(pos).line));
        pos += old_s.size();
      }

      if (count == 0) {
        // Edit distance closest line suggestion
        std::vector<std::string> lines;
        size_t lp = 0;
        while (lp < current_text.size()) {
          size_t next_lp = current_text.find('\n', lp);
          if (next_lp == std::string::npos) next_lp = current_text.size();
          lines.push_back(current_text.substr(lp, next_lp - lp));
          lp = next_lp + 1;
        }

        std::string closest_line;
        int min_d = 99999;
        int closest_lineno = 1;
        for (size_t i = 0; i < lines.size(); ++i) {
          int d = static_cast<int>(core::damerau_levenshtein(old_s, lines[i]));
          if (d < min_d) {
            min_d = d;
            closest_line = lines[i];
            closest_lineno = static_cast<int>(i + 1);
          }
        }

        violations.push_back(Violation{
            .code = "TEXT_NOT_FOUND",
            .phase = "EDIT",
            .message = "Text to replace not found in '" + e.file + "'.",
            .evidence = {{"closest_line", closest_line}, {"line", closest_lineno}},
            .fix_hint = "Verify exact whitespace and characters against current file contents."
        });
        return;
      }

      if (count > 1) {
        violations.push_back(Violation{
            .code = "TEXT_NOT_UNIQUE",
            .phase = "EDIT",
            .message = "Text occurs " + std::to_string(count) + " times in '" + e.file + "'. Must be unique.",
            .evidence = {{"occurrences", line_numbers}},
            .fix_hint = "Include more surrounding context in old_text to make it unique."
        });
        return;
      }

      // Replace single occurrence
      std::string updated = current_text.substr(0, found_pos) + new_s + current_text.substr(found_pos + old_s.size());
      buf.post = updated;
      buf.edited_ranges.push_back({static_cast<uint32_t>(found_pos), static_cast<uint32_t>(found_pos + new_s.size())});
      continue;
    }

    if (e.op == OpKind::ReplaceSymbol || e.op == OpKind::DeleteSymbol || e.op == OpKind::InsertSymbol) {
      // Re-extract facts from current buffer for fresh symbol ranges
      lang::FileFacts facts = lang::extract_any(e.file, current_text, buf.lang);

      if (e.op == OpKind::ReplaceSymbol || e.op == OpKind::DeleteSymbol) {
        std::string sym_query = *e.symbol;
        const lang::SymbolFact* target_sym = nullptr;
        std::vector<const lang::SymbolFact*> candidates;

        for (const auto& sym : facts.symbols) {
          if (sym.kind == lang::SymbolKind::Module) continue;
          if (sym.id == sym_query || sym.qualname == sym_query || sym.name == sym_query) {
            candidates.push_back(&sym);
          }
        }

        if (candidates.empty()) {
          // Find closest qualnames
          std::vector<std::string> pool;
          for (const auto& s : facts.symbols) {
            if (s.kind != lang::SymbolKind::Module) pool.push_back(s.qualname);
          }
          auto similar = find_similar_names(sym_query, pool, 5);

          violations.push_back(Violation{
              .code = "SYMBOL_NOT_FOUND",
              .phase = "EDIT",
              .message = "Symbol '" + sym_query + "' not found in '" + e.file + "'.",
              .evidence = {{"symbol", sym_query}, {"candidates", similar}},
              .fix_hint = "Check get_file_outline for exact symbol qualnames."
          });
          return;
        }

        if (candidates.size() > 1) {
          std::vector<std::string> cand_ids;
          for (const auto* c : candidates) cand_ids.push_back(c->id);

          violations.push_back(Violation{
              .code = "SYMBOL_AMBIGUOUS",
              .phase = "EDIT",
              .message = "Symbol '" + sym_query + "' is ambiguous in '" + e.file + "'.",
              .evidence = {{"candidates", cand_ids}},
              .fix_hint = "Use qualified name (e.g. ClassName." + sym_query + ")."
          });
          return;
        }

        target_sym = candidates[0];

        if (e.op == OpKind::DeleteSymbol) {
          // Remove definition range + leading whitespace + trailing newline
          uint32_t start = target_sym->def_range.start;
          uint32_t end = target_sym->def_range.end;

          // Expand start back to line beginning if whitespace
          size_t line_start = current_text.rfind('\n', start == 0 ? 0 : start - 1);
          line_start = (line_start == std::string_view::npos) ? 0 : line_start + 1;
          bool only_spaces = true;
          for (size_t i = line_start; i < start; ++i) {
            if (current_text[i] != ' ' && current_text[i] != '\t') {
              only_spaces = false;
              break;
            }
          }
          if (only_spaces) start = static_cast<uint32_t>(line_start);

          // Expand end past trailing newline
          if (end < current_text.size() && current_text[end] == '\r') ++end;
          if (end < current_text.size() && current_text[end] == '\n') ++end;

          std::string updated = current_text.substr(0, start) + current_text.substr(end);

          // If deleting last member of class body left empty class, insert pass
          if (!target_sym->class_qualname.empty() && buf.lang == lang::Language::Python) {
            // Check if class body now has no statements
            std::regex empty_class_re("class\\s+" + target_sym->class_qualname + "\\s*(\\(.*?\\))?\\s*:\\s*\\n(\\s*\\n)*");
            std::smatch m;
            if (std::regex_search(updated, m, empty_class_re)) {
              // Add pass
              std::string pass_str = "    pass\n";
              updated.insert(m.position() + m.length(), pass_str);
            }
          }

          buf.post = updated;
          continue;
        }

        // ReplaceSymbol
        std::string raw_code = *e.code;

        // Standalone parse check: must be exactly one symbol definition
        lang::FileFacts parsed_code_facts = lang::extract_any("snippet", raw_code, buf.lang);
        int def_count = 0;
        for (const auto& s : parsed_code_facts.symbols) {
          if (s.kind != lang::SymbolKind::Module) ++def_count;
        }
        if (def_count != 1) {
          violations.push_back(Violation{
              .code = "CODE_NOT_SINGLE_SYMBOL",
              .phase = "EDIT",
              .message = "Replacement code must contain exactly one top-level symbol definition.",
              .evidence = {{"found", def_count}},
              .fix_hint = "Provide full definition of exactly one function or class."
          });
          return;
        }

        // Reindent code
        std::string dedented = core::dedent(raw_code);
        std::string indent = get_indent_string(current_text, target_sym->def_range.start);
        std::string reindented = core::reindent(dedented, indent);
        reindented = ensure_eol(reindented, is_crlf);

        uint32_t start = target_sym->def_range.start;
        uint32_t end = target_sym->def_range.end;
        std::string updated = current_text.substr(0, start) + reindented + current_text.substr(end);

        buf.post = updated;
        buf.edited_ranges.push_back({start, static_cast<uint32_t>(start + reindented.size())});
        continue;
      }

      if (e.op == OpKind::InsertSymbol) {
        std::string raw_code = *e.code;
        lang::FileFacts parsed_code_facts = lang::extract_any("snippet", raw_code, buf.lang);
        int def_count = 0;
        for (const auto& s : parsed_code_facts.symbols) {
          if (s.kind != lang::SymbolKind::Module) ++def_count;
        }
        if (def_count != 1) {
          violations.push_back(Violation{
              .code = "CODE_NOT_SINGLE_SYMBOL",
              .phase = "EDIT",
              .message = "Inserted code must contain exactly one symbol definition.",
              .evidence = {{"found", def_count}},
              .fix_hint = "Provide full definition of exactly one function or class."
          });
          return;
        }

        std::string dedented = core::dedent(raw_code);
        size_t insert_pos = current_text.size();
        std::string indent_str = "";
        std::string sep_before = is_crlf ? "\r\n\r\n" : "\n\n";
        std::string sep_after = is_crlf ? "\r\n" : "\n";

        if (e.anchor.has_value()) {
          if (e.anchor->after.has_value()) {
            for (const auto& sym : facts.symbols) {
              if (sym.qualname == *e.anchor->after || sym.name == *e.anchor->after) {
                insert_pos = sym.def_range.end;
                break;
              }
            }
          } else if (e.anchor->before.has_value()) {
            for (const auto& sym : facts.symbols) {
              if (sym.qualname == *e.anchor->before || sym.name == *e.anchor->before) {
                insert_pos = sym.def_range.start;
                break;
              }
            }
          } else if (e.anchor->into_class.has_value()) {
            for (const auto& sym : facts.symbols) {
              if (sym.qualname == *e.anchor->into_class || sym.name == *e.anchor->into_class) {
                insert_pos = sym.body_range.end;
                indent_str = "    ";
                break;
              }
            }
          } else if (e.anchor->at_start) {
            insert_pos = 0;
            // Place after import statements
            for (const auto& imp : facts.imports) {
              if (imp.line > 0) {
                // approximate
              }
            }
          }
        }

        std::string reindented = core::reindent(dedented, indent_str);
        reindented = ensure_eol(reindented, is_crlf);

        std::string updated;
        if (insert_pos >= current_text.size()) {
          if (!current_text.empty() && current_text.back() != '\n') {
            current_text += (is_crlf ? "\r\n" : "\n");
          }
          updated = current_text + sep_before + reindented + sep_after;
        } else {
          updated = current_text.substr(0, insert_pos) + sep_before + reindented + sep_after + current_text.substr(insert_pos);
        }

        buf.post = updated;
        buf.edited_ranges.push_back({static_cast<uint32_t>(insert_pos), static_cast<uint32_t>(insert_pos + reindented.size())});
      }
    }
  }
}

void Gate::p2_syntax(std::map<std::string, Buffer>& ws,
                     std::vector<Violation>& violations,
                     std::vector<Violation>& warnings) {
  for (const auto& [file, buf] : ws) {
    if (!buf.post.has_value() || buf.is_deleted) continue;
    if (buf.lang == lang::Language::Unknown) continue;

    const std::string& post_str = *buf.post;
    lang::TreePtr post_tree = lang::parse_string(buf.lang, post_str);
    if (!post_tree) continue;

    TSNode root = ts_tree_root_node(post_tree.get());
    if (!ts_node_has_error(root)) continue;

    core::LineIndex line_idx(post_str);
    int error_count = 0;

    std::function<void(TSNode)> walk_errors = [&](TSNode n) {
      if (error_count >= 10) return;
      if (!ts_node_has_error(n)) return;

      bool is_err = ts_node_is_error(n);
      bool is_miss = ts_node_is_missing(n);

      if (is_err || is_miss) {
        uint32_t start_byte = ts_node_start_byte(n);
        TSPoint pt = ts_node_start_point(n);
        int line = static_cast<int>(pt.row + 1);
        int col = static_cast<int>(pt.column + 1);

        // Check if error was introduced (falls inside any edited_ranges)
        bool introduced = false;
        if (buf.is_new || buf.edited_ranges.empty()) {
          introduced = true;
        } else {
          for (const auto& r : buf.edited_ranges) {
            if (start_byte >= r.start && start_byte <= r.end) {
              introduced = true;
              break;
            }
          }
        }

        if (introduced) {
          ++error_count;
          std::string line_text = get_line_snippet(post_str, line);
          std::string pointer = std::string(col > 0 ? col - 1 : 0, ' ') + "^";
          std::string expected = is_miss ? ts_node_type(n) : "";

          std::string msg = "Syntax error in " + file + " at " + std::to_string(line) + ":" + std::to_string(col) +
                            (is_miss ? (": missing '" + expected + "'") : ": unexpected token");

          std::string fix_hint = "Fix the syntax at the marked position.";
          if (expected == ")" || expected == "]" || expected == "}" || expected == ":") {
            fix_hint = "Close the block/expression with '" + expected + "'.";
          }

          violations.push_back(Violation{
              .code = "SYNTAX_ERROR",
              .severity = Severity::Error,
              .phase = "SYNTAX",
              .message = msg,
              .location = Location{file, line, col},
              .evidence = {
                  {"kind", is_miss ? "missing" : "unexpected"},
                  {"expected", expected},
                  {"line_text", line_text.substr(0, 200)},
                  {"pointer", pointer}
              },
              .fix_hint = fix_hint
          });
        }
      }

      for (uint32_t i = 0; i < ts_node_child_count(n); ++i) {
        walk_errors(ts_node_child(n, i));
      }
    };

    walk_errors(root);

    if (error_count == 0) {
      warnings.push_back(Violation{
          .code = "PREEXISTING_SYNTAX_ERRORS",
          .severity = Severity::Info,
          .phase = "SYNTAX",
          .message = "Pre-existing syntax errors in '" + file + "' detected (not introduced by this patch)."
      });
    }
  }
}

void Gate::p3_symbol_diff(std::map<std::string, Buffer>& ws,
                          std::vector<FileChange>& changes,
                          std::vector<std::string>& targets,
                          std::vector<Violation>& warnings) {
  for (auto& [file, buf] : ws) {
    FileChange fc;
    fc.file = file;
    fc.pre_hash = buf.pre.has_value() ? core::hash_bytes(*buf.pre) : "";
    fc.post_hash = buf.post.has_value() ? core::hash_bytes(*buf.post) : "";

    if (buf.is_deleted) {
      fc.change = buf.moved_to.empty() ? "deleted" : "moved";
      for (const auto& s : buf.pre_facts.symbols) {
        if (s.kind != lang::SymbolKind::Module) {
          fc.symbols_removed.push_back(s.id);
          targets.push_back(s.id);
        }
      }
      changes.push_back(std::move(fc));
      continue;
    }

    if (buf.is_new) {
      fc.change = "created";
      if (buf.post.has_value()) {
        buf.post_facts = lang::extract_any(file, *buf.post, buf.lang);
        for (const auto& s : buf.post_facts.symbols) {
          if (s.kind != lang::SymbolKind::Module) {
            fc.symbols_added.push_back(s.id);
          }
        }
      }
      changes.push_back(std::move(fc));
      continue;
    }

    // Modified file
    fc.change = "modified";
    if (buf.post.has_value()) {
      buf.post_facts = lang::extract_any(file, *buf.post, buf.lang);

      std::unordered_map<std::string, const lang::SymbolFact*> pre_syms;
      for (const auto& s : buf.pre_facts.symbols) {
        if (s.kind != lang::SymbolKind::Module) pre_syms[s.id] = &s;
      }

      std::unordered_map<std::string, const lang::SymbolFact*> post_syms;
      for (const auto& s : buf.post_facts.symbols) {
        if (s.kind != lang::SymbolKind::Module) post_syms[s.id] = &s;
      }

      for (const auto& [id, post_s] : post_syms) {
        auto it = pre_syms.find(id);
        if (it == pre_syms.end()) {
          fc.symbols_added.push_back(id);
        } else {
          const auto* pre_s = it->second;
          if (pre_s->sig.canonical() != post_s->sig.canonical()) {
            fc.symbols_signature_changed.push_back(id);
            targets.push_back(id);
          } else if (pre_s->body_hash != post_s->body_hash) {
            fc.symbols_body_changed.push_back(id);
          }
        }
      }

      for (const auto& [id, pre_s] : pre_syms) {
        if (post_syms.find(id) == post_syms.end()) {
          fc.symbols_removed.push_back(id);
          targets.push_back(id);
        }
      }

      // Renamed check
      for (const auto& rem_id : fc.symbols_removed) {
        auto it_rem = pre_syms.find(rem_id);
        if (it_rem != pre_syms.end()) {
          for (const auto& add_id : fc.symbols_added) {
            auto it_add = post_syms.find(add_id);
            if (it_add != post_syms.end() &&
                it_rem->second->body_hash == it_add->second->body_hash &&
                it_rem->second->kind == it_add->second->kind) {
              warnings.push_back(Violation{
                  .code = "SYMBOL_RENAMED",
                  .severity = Severity::Info,
                  .message = "Symbol renamed from '" + rem_id + "' to '" + add_id + "'.",
                  .evidence = {{"from", rem_id}, {"to", add_id}}
              });
            }
          }
        }
      }
    }
    changes.push_back(std::move(fc));
  }
}

void Gate::p4_blast_radius(const std::map<std::string, Buffer>& ws,
                           const std::vector<std::string>& targets,
                           std::vector<Violation>& violations,
                           std::vector<Violation>& warnings) {
  if (!targets.empty()) {
    std::vector<std::string> exclude_files;
    for (const auto& [f, _] : ws) exclude_files.push_back(f);

  // 1. Callers of changed/removed targets from FalkorDB graph
  nlohmann::json caller_rows = nlohmann::json::array();
  if (engine_.client().ping()) {
    try {
      auto res = engine_.client().rows(engine_.config().project_graph(), db::Query{
          .name = "callers_of_ids",
          .cypher = db::Q_CALLERS_OF_IDS,
          .params = {
              {"ids", targets},
              {"exclude_files", exclude_files}
          },
          .read_only = true
      });
      for (const auto& row_json : res) {
        caller_rows.push_back(row_json);
      }
    } catch (...) {}
  }

  // 2. Combine with post-state call sites in WorkingSet
  for (const auto& [f, buf] : ws) {
    if (!buf.post.has_value() || buf.is_deleted) continue;
    auto resolved_calls = engine_.resolver().resolve_calls(engine_.fact_store(), buf.post_facts);
    for (const auto& r_call : resolved_calls) {
      for (const auto& t : targets) {
        if (r_call.target_id == t) {
          nlohmann::json r = {
              {"target", t},
              {"caller", r_call.caller_id},
              {"file", f},
              {"line", r_call.line},
              {"col", r_call.col},
              {"call", r_call.call_text},
              {"pos_args", r_call.pos_args},
              {"kw_names", r_call.kw_names},
              {"has_star", r_call.has_star},
              {"has_dstar", r_call.has_dstar},
              {"confidence", r_call.confidence},
              {"call_kind", r_call.call_kind}
          };
          caller_rows.push_back(std::move(r));
        }
      }
    }
  }

  // 3. Validate each caller against new post-state or removal
  for (const auto& row : caller_rows) {
    std::string target_id = row.value("target", "");
    std::string caller_file = row.value("file", "");
    int line = row.value("line", 0);
    int col = row.value("col", 0);
    std::string call_text = row.value("call", "");
    std::string conf = row.value("confidence", "exact");

    // Check if target is removed
    const lang::SymbolFact* post_sym = nullptr;
    for (const auto& [_, b] : ws) {
      if (b.post.has_value() && !b.is_deleted) {
        for (const auto& s : b.post_facts.symbols) {
          if (s.id == target_id) {
            post_sym = &s;
            break;
          }
        }
      }
    }

    if (!post_sym) {
      // Target was removed!
      violations.push_back(Violation{
          .code = "REMOVED_SYMBOL_IN_USE",
          .severity = (conf == "exact") ? Severity::Error : Severity::Warn,
          .phase = "BLAST_RADIUS",
          .message = caller_file + ":" + std::to_string(line) + " calls '" + target_id + "', which was removed.",
          .symbol_id = target_id,
          .location = Location{caller_file, line, col},
          .evidence = {{"callee", target_id}, {"call", call_text}, {"confidence", conf}},
          .fix_hint = "Migrate callers before removing symbol, or restore the symbol."
      });
      continue;
    }

    // Target signature changed: check call compatibility
    lang::CallSite cs;
    cs.pos_args = row.value("pos_args", 0);
    if (row.contains("kw_names") && row["kw_names"].is_array()) {
      for (const auto& k : row["kw_names"]) cs.kw_names.push_back(k.get<std::string>());
    }
    cs.has_star = row.value("has_star", false);
    cs.has_dstar = row.value("has_dstar", false);

    lang::CallKind kind = lang::CallKind::Plain;
    std::string k_str = row.value("call_kind", "");
    if (k_str == "method") kind = lang::CallKind::InstanceMethod;
    else if (k_str == "constructor") kind = lang::CallKind::Constructor;

    lang::Language lang = lang::LanguageRegistry::get().for_path(caller_file).lang;
    auto bind_res = lang::BindingChecker::check(post_sym->sig, cs, kind, lang);

    if (bind_res.verdict == lang::BindResult::Incompatible) {
      violations.push_back(Violation{
          .code = "CALLER_INCOMPATIBLE",
          .severity = (conf == "exact") ? Severity::Error : Severity::Warn,
          .phase = "BLAST_RADIUS",
          .message = caller_file + ":" + std::to_string(line) + " calls " + call_text +
                     " but the new signature is incompatible (" + bind_res.reason + ").",
          .symbol_id = target_id,
          .location = Location{caller_file, line, col},
          .evidence = {
              {"callee", target_id},
              {"new_signature", post_sym->sig.canonical()},
              {"call", call_text},
              {"reason", bind_res.reason},
              {"confidence", conf}
          },
          .fix_hint = "Update this call in the same propose_patch (add an edit for " + caller_file +
                      "), or give new parameters default values."
      });
    } else if (bind_res.verdict == lang::BindResult::Unknown) {
      warnings.push_back(Violation{
          .code = "CALLER_UNCHECKED",
          .severity = Severity::Info,
          .phase = "BLAST_RADIUS",
          .message = caller_file + ":" + std::to_string(line) + " calls " + call_text + " with splat/overloads.",
          .symbol_id = target_id,
          .location = Location{caller_file, line, col}
      });
    }
  }
  }

  // 4. Outgoing references & Anti-hallucination checks
  for (const auto& [f, buf] : ws) {
    if (!buf.post.has_value() || buf.is_deleted) continue;

    // Check imports
    for (const auto& imp : buf.post_facts.imports) {
      if (imp.kind == lang::ImportFact::FromNames || imp.kind == lang::ImportFact::TsNamed) {
        // Find target module
        std::string mod_file;
        if (buf.lang == lang::Language::Python) {
          mod_file = engine_.resolver().resolve_python_module(engine_.fact_store(), f, imp.spec);
        } else {
          mod_file = engine_.resolver().resolve_ts_module(engine_.fact_store(), f, imp.spec);
        }

        // If not found in fact_store, check if it's in the working set (e.g. newly created file)
        if (mod_file.empty()) {
          for (const auto& [ws_path, ws_buf] : ws) {
            if (!ws_buf.post.has_value() || ws_buf.is_deleted) continue;
            if (ws_path == imp.spec || ws_path == imp.spec + ".py" || ws_path == imp.spec + ".ts" || ws_path == imp.spec + ".js") {
              mod_file = ws_path;
              break;
            }
            if (imp.spec.starts_with("./") || imp.spec.starts_with("../")) {
              std::filesystem::path base = std::filesystem::path(f).parent_path() / imp.spec;
              std::string norm = base.lexically_normal().generic_string();
              if (ws_path == norm || ws_path == norm + ".ts" || ws_path == norm + ".js" || ws_path == norm + ".tsx") {
                mod_file = ws_path;
                break;
              }
            }
          }
        }

        bool should_check_missing = imp.spec.starts_with(".") || imp.spec.starts_with("/");
        if (!should_check_missing && buf.lang == lang::Language::Python) {
          static const std::unordered_set<std::string_view> PY_STDLIB = {
            "os", "sys", "re", "math", "json", "time", "datetime", "typing", "collections",
            "itertools", "functools", "pathlib", "unittest", "logging", "shutil", "tempfile",
            "hashlib", "random", "string", "subprocess", "threading", "multiprocessing", "asyncio",
            "socket", "http", "urllib", "email", "io", "csv", "sqlite3", "copy", "inspect",
            "abc", "enum", "dataclasses", "contextlib", "decimal", "fractions", "operator", "types",
            "warnings", "traceback", "sysconfig", "platform", "ctypes", "struct", "pickle", "uuid",
            "doctest", "difflib", "calendar", "glob", "fnmatch", "gzip", "bz2", "lzma", "zipfile",
            "tarfile", "concurrent", "queue", "base64", "binascii", "quopri", "html", "xml"
          };
          std::string_view top_mod = imp.spec;
          size_t dot = top_mod.find('.');
          if (dot != std::string_view::npos) top_mod = top_mod.substr(0, dot);
          if (!PY_STDLIB.contains(top_mod)) {
            should_check_missing = true;
          }
        }

        if (mod_file.empty() && should_check_missing) {
          violations.push_back(Violation{
              .code = "MISSING_MODULE",
              .severity = Severity::Error,
              .phase = "REFERENCES",
              .message = f + ":" + std::to_string(imp.line) + " imports from '" + imp.spec + "', which does not exist.",
              .location = Location{f, static_cast<int>(imp.line), 1},
              .evidence = {{"import", imp.spec}}
          });
          continue;
        }

        if (!mod_file.empty()) {
          // Check if module exists in workspace or ws
          bool exists = false;
          auto it_ws = ws.find(mod_file);
          if (it_ws != ws.end()) {
            exists = it_ws->second.post.has_value() && !it_ws->second.is_deleted;
          } else {
            std::error_code ec;
            exists = std::filesystem::exists(engine_.config().root / platform::from_utf8(mod_file), ec);
          }

          if (!exists) {
            violations.push_back(Violation{
                .code = "MISSING_MODULE",
                .severity = Severity::Error,
                .phase = "REFERENCES",
                .message = f + ":" + std::to_string(imp.line) + " imports from '" + imp.spec + "', which does not exist.",
                .location = Location{f, static_cast<int>(imp.line), 1},
                .evidence = {{"import", imp.spec}}
            });
            continue;
          }

          // Check if imported names exist
          const lang::FileFacts* mod_facts = nullptr;
          if (it_ws != ws.end() && it_ws->second.post.has_value() && !it_ws->second.is_deleted) {
            mod_facts = &it_ws->second.post_facts;
          } else {
            mod_facts = engine_.fact_store().get(mod_file);
          }

          if (mod_facts) {
            std::vector<std::string> mod_symbols;
            for (const auto& s : mod_facts->symbols) {
              if (s.kind != lang::SymbolKind::Module) mod_symbols.push_back(s.name);
            }
            for (const auto& exp : mod_facts->exports) mod_symbols.push_back(exp);

            for (const auto& [name, alias] : imp.names) {
              if (name == "*") continue;
              bool found = false;
              for (const auto& s_name : mod_symbols) {
                if (s_name == name) { found = true; break; }
              }
              if (!found) {
                auto did_you_mean = find_similar_names(name, mod_symbols, 3);
                violations.push_back(Violation{
                    .code = "MISSING_SYMBOL",
                    .severity = Severity::Error,
                    .phase = "REFERENCES",
                    .message = f + ":" + std::to_string(imp.line) + " imports '" + name + "' from '" + mod_file +
                               "', which does not exist.",
                    .location = Location{f, static_cast<int>(imp.line), 1},
                    .evidence = {
                        {"module", mod_file},
                        {"name", name},
                        {"did_you_mean", did_you_mean}
                    },
                    .fix_hint = did_you_mean.empty() ? "Check get_file_outline for available exports."
                                                     : ("Did you mean: " + format_did_you_mean(did_you_mean))
                });
              }
            }
          }
        }
      }
    }
  }
}

void Gate::p5_quality_rules(const std::map<std::string, Buffer>& ws,
                            std::vector<Violation>& violations,
                            std::vector<Violation>& warnings) {
  for (const auto& [file, buf] : ws) {
    if (!buf.post.has_value() || buf.is_deleted) continue;
    if (buf.lang == lang::Language::Unknown) continue;

    auto file_violations = rules_engine_.check(file, buf.lang, *buf.post, buf.post_facts, buf.edited_ranges);
    for (auto& v : file_violations) {
      if (v.severity == Severity::Error) {
        violations.push_back(std::move(v));
      } else {
        warnings.push_back(std::move(v));
      }
    }
  }
}

void Gate::p6_write(const std::string& patch_id,
                    const PatchRequest& req,
                    const memory::SessionCtx& session,
                    std::map<std::string, Buffer>& ws,
                    const std::vector<FileChange>& changes) {
  std::vector<TxnFile> txn_files;
  std::map<std::string, std::string> post_contents;
  std::vector<std::string> created_dirs;

  for (const auto& [f, buf] : ws) {
    TxnFile tf;
    tf.path = f;
    if (buf.is_deleted) {
      tf.op = buf.moved_to.empty() ? "deleted" : "moved";
      tf.to_path = buf.moved_to;
    } else if (buf.is_new) {
      tf.op = "created";
      post_contents[f] = buf.post.value_or("");
    } else {
      tf.op = "modified";
      post_contents[f] = buf.post.value_or("");
    }
    tf.pre_hash = buf.pre.has_value() ? core::hash_bytes(*buf.pre) : "";
    tf.post_hash = buf.post.has_value() ? core::hash_bytes(*buf.post) : "";
    txn_files.push_back(std::move(tf));
  }

  std::vector<std::string> files_list;
  std::vector<std::string> symbols_list;
  for (const auto& c : changes) {
    files_list.push_back(c.file);
    for (const auto& s : c.symbols_added) symbols_list.push_back(s);
    for (const auto& s : c.symbols_signature_changed) symbols_list.push_back(s);
    for (const auto& s : c.symbols_body_changed) symbols_list.push_back(s);
  }

  nlohmann::json decision_payload = {
      {"patch_id", patch_id},
      {"status", "APPLIED"},
      {"actor", session.client},
      {"intent", req.intent},
      {"reasoning", req.reasoning},
      {"failed_phase", ""},
      {"violation_codes", nlohmann::json::array()},
      {"files", files_list},
      {"symbols", symbols_list},
      {"ts", core::now_ms()},
      {"duration_ms", 0}
  };

  DiskTxn txn(engine_.config().root, patch_id);
  txn.prepare(txn_files, created_dirs, post_contents, decision_payload);
  txn.commit();

  // Update in-memory FactStore & Resolver
  for (auto& [f, buf] : ws) {
    if (buf.is_deleted) {
      engine_.fact_store().erase(f);
    } else if (buf.post.has_value()) {
      engine_.fact_store().put(std::move(buf.post_facts));
    }
  }
  engine_.resolver().rebuild_indices(engine_.fact_store());

  // Update FalkorDB graph
  if (engine_.client().ping()) {
    try {
      int64_t now = core::now_ms();
      std::vector<const lang::FileFacts*> updated_facts;
      std::vector<std::string> deleted_files;
      for (const auto& [f, buf] : ws) {
        if (buf.is_deleted) {
          deleted_files.push_back(f);
        } else if (buf.post.has_value()) {
          const auto* facts = engine_.fact_store().get(f);
          if (facts) updated_facts.push_back(facts);
        }
      }

      if (!deleted_files.empty()) {
        engine_.graph_writer().delete_files(deleted_files);
      }
      if (!updated_facts.empty()) {
        std::vector<index::ResolvedImport> all_imports;
        std::vector<index::ResolvedInherit> all_inherits;
        for (const auto* facts : updated_facts) {
          auto imps = engine_.resolver().resolve_imports_for_file(engine_.fact_store(), *facts);
          all_imports.insert(all_imports.end(), imps.begin(), imps.end());
          auto inhs = engine_.resolver().resolve_inherits_for_file(engine_.fact_store(), *facts);
          all_inherits.insert(all_inherits.end(), inhs.begin(), inhs.end());
        }
        engine_.graph_writer().sync_files(updated_facts, all_imports, all_inherits, now);
      }

      // Record Decision
      engine_.client().exec(engine_.config().project_graph(), db::Query{
          .name = "decision_record",
          .cypher = db::Q_DECISION_RECORD,
          .params = {
              {"d", decision_payload},
              {"session", session.session_id},
              {"intent_id", session.current_intent_id}
          },
          .read_only = false
      });

      // Link Mutated symbols
      nlohmann::json mutated_rows = nlohmann::json::array();
      for (const auto& c : changes) {
        for (const auto& s : c.symbols_added) mutated_rows.push_back({{"id", s}, {"change", "added"}});
        for (const auto& s : c.symbols_removed) mutated_rows.push_back({{"id", s}, {"change", "removed"}});
        for (const auto& s : c.symbols_signature_changed) mutated_rows.push_back({{"id", s}, {"change", "sig_changed"}});
        for (const auto& s : c.symbols_body_changed) mutated_rows.push_back({{"id", s}, {"change", "body_changed"}});
      }
      if (!mutated_rows.empty()) {
        engine_.client().exec(engine_.config().project_graph(), db::Query{
            .name = "decision_link_mutated",
            .cypher = db::Q_DECISION_LINK_MUTATED,
            .params = {
                {"patch_id", patch_id},
                {"rows", mutated_rows}
            },
            .read_only = false
        });
      }

      // Resolve pitfalls for touched symbols
      if (!symbols_list.empty()) {
        engine_.client().exec(engine_.config().project_graph(), db::Query{
            .name = "pitfalls_resolve",
            .cypher = db::Q_PITFALLS_RESOLVE,
            .params = {
                {"symbol_ids", symbols_list},
                {"now", now}
            },
            .read_only = false
        });
      }
    } catch (...) {}
  }

  txn.cleanup();
}

void Gate::record_rejected(const std::string& patch_id,
                           const PatchRequest& req,
                           const memory::SessionCtx& session,
                           const std::string& failed_phase,
                           const std::vector<Violation>& violations,
                           const std::map<std::string, Buffer>& ws) {
  if (!engine_.client().ping()) return;

  std::vector<std::string> files_list;
  for (const auto& [f, _] : ws) files_list.push_back(f);

  std::vector<std::string> codes;
  for (const auto& v : violations) codes.push_back(v.code);

  nlohmann::json decision_payload = {
      {"patch_id", patch_id},
      {"status", "REJECTED"},
      {"actor", session.client},
      {"intent", req.intent},
      {"reasoning", req.reasoning},
      {"failed_phase", failed_phase},
      {"violation_codes", codes},
      {"files", files_list},
      {"symbols", nlohmann::json::array()},
      {"ts", core::now_ms()},
      {"duration_ms", 0}
  };

  try {
    engine_.client().exec(engine_.config().project_graph(), db::Query{
        .name = "decision_record",
        .cypher = db::Q_DECISION_RECORD,
        .params = {
            {"d", decision_payload},
            {"session", session.session_id},
            {"intent_id", session.current_intent_id}
        },
        .read_only = false
    });

    nlohmann::json pitfall_rows = nlohmann::json::array();
    for (const auto& v : violations) {
      if (v.severity != Severity::Error) continue;
      std::string sym = v.symbol_id.value_or("");
      std::string file = v.location.has_value() ? v.location->file : "";
      std::string rule = v.rule_id.value_or("");

      std::string key = core::xxh3_hex(v.code + "|" + (sym.empty() ? file : sym) + "|" + rule);

      std::string lesson = v.message;
      if (v.code == "CALLER_INCOMPATIBLE") {
        lesson = "Changing the signature of `" + sym + "` breaks callers. Update callers in the same patch or keep backward-compatible defaults.";
      } else if (v.code == "REMOVED_SYMBOL_IN_USE") {
        lesson = "`" + sym + "` is still called; migrate callers before removing it.";
      } else if (v.code == "MISSING_SYMBOL") {
        lesson = "Missing symbol in import/call. Check get_file_outline before calling helpers.";
      }

      pitfall_rows.push_back({
          {"key", key},
          {"code", v.code},
          {"rule_id", rule},
          {"symbol_id", sym},
          {"file", file},
          {"message", v.message},
          {"lesson", lesson}
      });
    }

    if (!pitfall_rows.empty()) {
      engine_.client().exec(engine_.config().project_graph(), db::Query{
          .name = "pitfalls_upsert",
          .cypher = db::Q_PITFALLS_UPSERT,
          .params = {
              {"patch_id", patch_id},
              {"rows", pitfall_rows},
              {"now", core::now_ms()}
          },
          .read_only = false
      });
    }
  } catch (...) {}
}

std::vector<nlohmann::json> Gate::fetch_lessons(const std::vector<std::string>& targets,
                                                const std::map<std::string, Buffer>& ws) {
  std::vector<nlohmann::json> lessons;
  if (!engine_.client().ping()) return lessons;

  std::vector<std::string> files;
  for (const auto& [f, _] : ws) files.push_back(f);

  try {
    lessons = engine_.client().rows(engine_.config().project_graph(), db::Query{
        .name = "lessons_for",
        .cypher = db::Q_LESSONS_FOR,
        .params = {
            {"symbol_ids", targets},
            {"files", files},
            {"limit", 5}
        },
        .read_only = true
    });
  } catch (...) {}

  return lessons;
}

std::vector<Violation> Gate::audit_cumulative(std::map<std::string, Buffer>& ws) {
  std::vector<FileChange> changes;
  std::vector<std::string> targets;
  std::vector<Violation> warnings;
  std::vector<Violation> violations;

  p3_symbol_diff(ws, changes, targets, warnings);
  p4_blast_radius(ws, targets, violations, warnings);

  return violations;
}

} // namespace gs::gate
