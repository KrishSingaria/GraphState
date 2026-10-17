#include "gate/patch.hpp"

namespace gs::gate {

std::string_view op_kind_name(OpKind op) {
  switch (op) {
    case OpKind::ReplaceSymbol: return "replace_symbol";
    case OpKind::InsertSymbol:  return "insert_symbol";
    case OpKind::DeleteSymbol:  return "delete_symbol";
    case OpKind::ReplaceText:   return "replace_text";
    case OpKind::CreateFile:    return "create_file";
    case OpKind::DeleteFile:    return "delete_file";
    case OpKind::MoveFile:      return "move_file";
  }
  return "unknown";
}

std::optional<OpKind> parse_op_kind(std::string_view s) {
  if (s == "replace_symbol") return OpKind::ReplaceSymbol;
  if (s == "insert_symbol")  return OpKind::InsertSymbol;
  if (s == "delete_symbol")  return OpKind::DeleteSymbol;
  if (s == "replace_text")   return OpKind::ReplaceText;
  if (s == "create_file")    return OpKind::CreateFile;
  if (s == "delete_file")    return OpKind::DeleteFile;
  if (s == "move_file")      return OpKind::MoveFile;
  return std::nullopt;
}

std::string_view severity_name(Severity s) {
  switch (s) {
    case Severity::Error: return "error";
    case Severity::Warn:  return "warn";
    case Severity::Info:  return "info";
  }
  return "error";
}

std::string_view patch_status_name(PatchStatus s) {
  switch (s) {
    case PatchStatus::Applied:   return "APPLIED";
    case PatchStatus::Rejected:  return "REJECTED";
    case PatchStatus::Validated: return "VALIDATED";
  }
  return "REJECTED";
}

nlohmann::json to_json(const Location& loc) {
  return {
      {"file", loc.file},
      {"line", loc.line},
      {"col", loc.col}
  };
}

nlohmann::json to_json(const Violation& v) {
  nlohmann::json j = {
      {"code", v.code},
      {"severity", severity_name(v.severity)},
      {"phase", v.phase},
      {"message", v.message},
      {"evidence", v.evidence},
      {"fix_hint", v.fix_hint}
  };
  if (v.symbol_id.has_value()) {
    j["symbol_id"] = *v.symbol_id;
  }
  if (v.location.has_value()) {
    j["location"] = to_json(*v.location);
  }
  if (v.rule_id.has_value()) {
    j["rule_id"] = *v.rule_id;
  }
  return j;
}

nlohmann::json to_json(const FileChange& fc) {
  return {
      {"file", fc.file},
      {"change", fc.change},
      {"pre_hash", fc.pre_hash},
      {"post_hash", fc.post_hash},
      {"symbols_added", fc.symbols_added},
      {"symbols_removed", fc.symbols_removed},
      {"symbols_signature_changed", fc.symbols_signature_changed},
      {"symbols_body_changed", fc.symbols_body_changed}
  };
}

nlohmann::json to_json(const PatchResult& r) {
  nlohmann::json violations_json = nlohmann::json::array();
  for (const auto& v : r.violations) {
    violations_json.push_back(to_json(v));
  }

  nlohmann::json warnings_json = nlohmann::json::array();
  for (const auto& w : r.warnings) {
    warnings_json.push_back(to_json(w));
  }

  nlohmann::json changes_json = nlohmann::json::array();
  for (const auto& c : r.changes) {
    changes_json.push_back(to_json(c));
  }

  nlohmann::json lessons_json = r.lessons;

  nlohmann::json res = {
      {"patch_id", r.patch_id},
      {"status", patch_status_name(r.status)},
      {"failed_phase", r.failed_phase.has_value() ? nlohmann::json(*r.failed_phase) : nlohmann::json(nullptr)},
      {"violations", std::move(violations_json)},
      {"warnings", std::move(warnings_json)},
      {"changes", std::move(changes_json)},
      {"lessons", std::move(lessons_json)},
      {"timings_ms", r.timings_ms}
  };

  if (r.suggestion.has_value()) {
    res["suggestion"] = *r.suggestion;
  }

  if (r.changeset.has_value()) {
    res["changeset"] = *r.changeset;
  }

  return res;
}

PatchRequest parse_patch_request(const nlohmann::json& j) {
  PatchRequest req;
  if (j.contains("intent") && j["intent"].is_string()) {
    req.intent = j["intent"].get<std::string>();
  }
  if (j.contains("reasoning") && j["reasoning"].is_string()) {
    req.reasoning = j["reasoning"].get<std::string>();
  }
  if (j.contains("dry_run") && j["dry_run"].is_boolean()) {
    req.dry_run = j["dry_run"].get<bool>();
  }
  if (j.contains("base_hashes") && j["base_hashes"].is_object()) {
    for (auto it = j["base_hashes"].begin(); it != j["base_hashes"].end(); ++it) {
      if (it.value().is_string()) {
        req.base_hashes[it.key()] = it.value().get<std::string>();
      }
    }
  }

  if (j.contains("edits") && j["edits"].is_array()) {
    for (const auto& e : j["edits"]) {
      Edit edit;
      if (e.contains("op") && e["op"].is_string()) {
        auto kind = parse_op_kind(e["op"].get<std::string>());
        if (kind.has_value()) {
          edit.op = *kind;
        }
      }
      if (e.contains("file") && e["file"].is_string()) {
        edit.file = e["file"].get<std::string>();
      }
      if (e.contains("symbol") && e["symbol"].is_string()) {
        edit.symbol = e["symbol"].get<std::string>();
      }
      if (e.contains("code") && e["code"].is_string()) {
        edit.code = e["code"].get<std::string>();
      }
      if (e.contains("old_text") && e["old_text"].is_string()) {
        edit.old_text = e["old_text"].get<std::string>();
      }
      if (e.contains("new_text") && e["new_text"].is_string()) {
        edit.new_text = e["new_text"].get<std::string>();
      }
      if (e.contains("content") && e["content"].is_string()) {
        edit.content = e["content"].get<std::string>();
      }
      if (e.contains("to") && e["to"].is_string()) {
        edit.to = e["to"].get<std::string>();
      }
      if (e.contains("anchor") && e["anchor"].is_object()) {
        InsertAnchor anchor;
        const auto& a = e["anchor"];
        if (a.contains("after") && a["after"].is_string()) {
          anchor.after = a["after"].get<std::string>();
        }
        if (a.contains("before") && a["before"].is_string()) {
          anchor.before = a["before"].get<std::string>();
        }
        if (a.contains("into_class") && a["into_class"].is_string()) {
          anchor.into_class = a["into_class"].get<std::string>();
        }
        if (a.contains("at_start") && a["at_start"].is_boolean()) {
          anchor.at_start = a["at_start"].get<bool>();
        }
        edit.anchor = anchor;
      }
      req.edits.push_back(std::move(edit));
    }
  }

  return req;
}

} // namespace gs::gate
