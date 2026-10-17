#include "gate/audit.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>
#include "index/resolver.hpp"
#include "index/scanner.hpp"
#include "lang/binding.hpp"
#include "lang/extractor.hpp"
#include "lang/parser.hpp"
#include "lang/syntax.hpp"
#include "platform/platform.hpp"

namespace gs::gate {

nlohmann::json AuditReport::to_json() const {
  nlohmann::json j;
  j["findings"] = nlohmann::json::array();
  for (const auto& f : findings) {
    nlohmann::json fj;
    fj["code"] = f.code;
    fj["file"] = f.file;
    fj["line"] = f.line;
    fj["col"] = f.col;
    fj["message"] = f.message;
    if (!f.symbol.empty()) fj["symbol"] = f.symbol;
    if (!f.callee.empty()) fj["callee"] = f.callee;
    if (!f.evidence.is_null() && !f.evidence.empty()) fj["evidence"] = f.evidence;
    j["findings"].push_back(std::move(fj));
  }
  j["counts"] = counts;
  j["files"] = files;
  return j;
}

AuditReport diff_audit_reports(
    const AuditReport& base_report,
    const AuditReport& current_report) {
  std::unordered_set<std::string> base_keys;
  for (const auto& bf : base_report.findings) {
    base_keys.insert(bf.diff_key());
  }

  AuditReport diff_report;
  diff_report.files = current_report.files;

  std::unordered_set<std::string> seen;
  for (const auto& cf : current_report.findings) {
    std::string key = cf.diff_key();
    if (!base_keys.contains(key) && !seen.contains(key)) {
      seen.insert(key);
      diff_report.findings.push_back(cf);
      diff_report.counts[cf.code]++;
    }
  }

  return diff_report;
}

AuditReport audit_fact_store(
    const core::Config& config,
    const index::FactStore& store,
    const std::vector<AuditFinding>& syntax_errors) {
  AuditReport report;
  report.files = store.file_count();

  std::unordered_set<std::string> seen_keys;

  // Add syntax errors
  for (const auto& err : syntax_errors) {
    std::string k = err.diff_key();
    if (!seen_keys.contains(k)) {
      seen_keys.insert(k);
      report.findings.push_back(err);
      report.counts[err.code]++;
    }
  }

  index::Resolver resolver(config);

  for (const auto& f : store.all_files()) {
    const auto* facts = store.get(f);
    if (!facts) continue;

    auto imports_by_name = resolver.build_import_map(store, *facts);

    // 1. Check Imports
    if (facts->lang == lang::Language::Python) {
      for (const auto& imp : facts->imports) {
        if (imp.spec.starts_with(".")) {
          // Relative import
          std::string target_file = resolver.resolve_python_module(store, f, imp.spec);
          if (target_file.empty()) {
            AuditFinding finding{
                .code = "MISSING_MODULE",
                .file = f,
                .line = static_cast<int>(imp.line),
                .col = 1,
                .message = "Module '" + imp.spec + "' not found in workspace",
                .symbol = f,
                .callee = imp.spec,
                .evidence = {{"import_source", imp.spec}}
            };
            std::string k = finding.diff_key();
            if (!seen_keys.contains(k)) {
              seen_keys.insert(k);
              report.counts[finding.code]++;
              report.findings.push_back(std::move(finding));
            }
            continue;
          }

          if (imp.kind == lang::ImportFact::FromNames) {
            const auto* target_facts = store.get(target_file);
            for (const auto& [orig, alias] : imp.names) {
              if (orig == "*") continue;
              std::string sub_spec = imp.spec.empty() ? orig : (imp.spec + "." + orig);
              std::string sub_target = resolver.resolve_python_module(store, f, sub_spec);
              if (!sub_target.empty()) continue;

              bool found = false;
              if (target_facts) {
                for (const auto& s : target_facts->symbols) {
                  if (s.name == orig || s.qualname == orig) { found = true; break; }
                }
                if (!found) {
                  for (const auto& mn : target_facts->module_names) {
                    if (mn == orig) { found = true; break; }
                  }
                }
                if (!found) {
                  for (const auto& ti : target_facts->imports) {
                    for (const auto& [to, ta] : ti.names) {
                      if (ta == orig || to == orig) { found = true; break; }
                    }
                    if (found) break;
                  }
                }
              }
              if (!found) {
                AuditFinding finding{
                    .code = "BROKEN_IMPORT",
                    .file = f,
                    .line = static_cast<int>(imp.line),
                    .col = 1,
                    .message = "Cannot import name '" + orig + "' from '" + imp.spec + "' (" + target_file + ")",
                    .symbol = f,
                    .callee = target_file + "::" + orig,
                    .evidence = {{"module", imp.spec}, {"symbol", orig}, {"target_file", target_file}}
                };
                std::string k = finding.diff_key();
                if (!seen_keys.contains(k)) {
                  seen_keys.insert(k);
                  report.counts[finding.code]++;
                  report.findings.push_back(std::move(finding));
                }
              }
            }
          }
        } else {
          // Absolute import
          std::string target_file = resolver.resolve_python_module(store, f, imp.spec);
          if (!target_file.empty()) {
            if (imp.kind == lang::ImportFact::FromNames) {
              const auto* target_facts = store.get(target_file);
              for (const auto& [orig, alias] : imp.names) {
                if (orig == "*") continue;
                std::string sub_spec = imp.spec.empty() ? orig : (imp.spec + "." + orig);
                std::string sub_target = resolver.resolve_python_module(store, f, sub_spec);
                if (!sub_target.empty()) continue;

                bool found = false;
                if (target_facts) {
                  for (const auto& s : target_facts->symbols) {
                    if (s.name == orig || s.qualname == orig) { found = true; break; }
                  }
                  if (!found) {
                    for (const auto& mn : target_facts->module_names) {
                      if (mn == orig) { found = true; break; }
                    }
                  }
                  if (!found) {
                    for (const auto& ti : target_facts->imports) {
                      for (const auto& [to, ta] : ti.names) {
                        if (ta == orig || to == orig) { found = true; break; }
                      }
                      if (found) break;
                    }
                  }
                }
                if (!found) {
                  AuditFinding finding{
                      .code = "BROKEN_IMPORT",
                      .file = f,
                      .line = static_cast<int>(imp.line),
                      .col = 1,
                      .message = "Cannot import name '" + orig + "' from '" + imp.spec + "' (" + target_file + ")",
                      .symbol = f,
                      .callee = target_file + "::" + orig,
                      .evidence = {{"module", imp.spec}, {"symbol", orig}, {"target_file", target_file}}
                  };
                  std::string k = finding.diff_key();
                  if (!seen_keys.contains(k)) {
                    seen_keys.insert(k);
                    report.counts[finding.code]++;
                    report.findings.push_back(std::move(finding));
                  }
                }
              }
            }
          } else {
            // Check if this appears to be a missing workspace module
            size_t dot = imp.spec.find('.');
            std::string top = (dot == std::string::npos) ? imp.spec : imp.spec.substr(0, dot);
            bool is_ws_pkg = false;
            for (const auto& ex : store.all_files()) {
              if (ex.starts_with(top + "/")) {
                is_ws_pkg = true;
                break;
              }
            }
            if (is_ws_pkg) {
              AuditFinding finding{
                  .code = "MISSING_MODULE",
                  .file = f,
                  .line = static_cast<int>(imp.line),
                  .col = 1,
                  .message = "Workspace module '" + imp.spec + "' does not exist",
                  .symbol = f,
                  .callee = imp.spec,
                  .evidence = {{"import_source", imp.spec}}
              };
              std::string k = finding.diff_key();
              if (!seen_keys.contains(k)) {
                seen_keys.insert(k);
                report.counts[finding.code]++;
                report.findings.push_back(std::move(finding));
              }
            }
          }
        }
      }
    } else if (facts->lang == lang::Language::TypeScript ||
               facts->lang == lang::Language::Tsx ||
               facts->lang == lang::Language::JavaScript) {
      for (const auto& imp : facts->imports) {
        if (imp.spec.starts_with("./") || imp.spec.starts_with("../")) {
          std::string target_file = resolver.resolve_ts_module(store, f, imp.spec);
          if (target_file.empty()) {
            AuditFinding finding{
                .code = "MISSING_MODULE",
                .file = f,
                .line = static_cast<int>(imp.line),
                .col = 1,
                .message = "Cannot find module '" + imp.spec + "' in workspace",
                .symbol = f,
                .callee = imp.spec,
                .evidence = {{"import_source", imp.spec}}
            };
            std::string k = finding.diff_key();
            if (!seen_keys.contains(k)) {
              seen_keys.insert(k);
              report.counts[finding.code]++;
              report.findings.push_back(std::move(finding));
            }
          } else if (imp.kind != lang::ImportFact::TsNamespace) {
            const auto* target_facts = store.get(target_file);
            if (target_facts) {
              for (const auto& [orig, alias] : imp.names) {
                if (orig == "*" || orig == "default") continue;
                bool found = false;
                for (const auto& s : target_facts->symbols) {
                  if (s.name == orig || s.qualname == orig) { found = true; break; }
                }
                if (!found) {
                  for (const auto& ti : target_facts->imports) {
                    for (const auto& [to, ta] : ti.names) {
                      if (ta == orig || to == orig) { found = true; break; }
                    }
                    if (found) break;
                  }
                }
                if (!found) {
                  AuditFinding finding{
                      .code = "BROKEN_IMPORT",
                      .file = f,
                      .line = static_cast<int>(imp.line),
                      .col = 1,
                      .message = "Module '" + target_file + "' has no exported member '" + orig + "'",
                      .symbol = f,
                      .callee = target_file + "::" + orig,
                      .evidence = {{"module", imp.spec}, {"symbol", orig}, {"target_file", target_file}}
                  };
                  std::string k = finding.diff_key();
                  if (!seen_keys.contains(k)) {
                    seen_keys.insert(k);
                    report.counts[finding.code]++;
                    report.findings.push_back(std::move(finding));
                  }
                }
              }
            }
          }
        }
      }
    }

    // 2. Check Call Binding (CALL_INCOMPATIBLE)
    auto resolved_calls = resolver.resolve_calls(store, *facts);
    for (const auto& call : resolved_calls) {
      if (call.confidence == "exact") {
        const auto* callee_sym = store.find_symbol_by_id(call.target_id);
        if (callee_sym) {
          lang::CallSite cs;
          cs.pos_args = call.pos_args;
          cs.kw_names = call.kw_names;
          cs.has_star = call.has_star;
          cs.has_dstar = call.has_dstar;

          lang::CallKind kind = lang::CallKind::Plain;
          if (call.call_kind == "instance") kind = lang::CallKind::InstanceMethod;
          else if (call.call_kind == "classmethod") kind = lang::CallKind::ClassMethodViaClass;
          else if (call.call_kind == "constructor") kind = lang::CallKind::Constructor;
          else if (call.call_kind == "unbound") kind = lang::CallKind::Unbound;

          auto bind_res = lang::BindingChecker::check(callee_sym->sig, cs, kind, facts->lang);
          if (bind_res.verdict == lang::BindResult::Incompatible) {
            AuditFinding finding{
                .code = "CALL_INCOMPATIBLE",
                .file = f,
                .line = call.line,
                .col = call.col,
                .message = "Call to " + callee_sym->name + " is incompatible: " + bind_res.reason,
                .symbol = call.caller_id,
                .callee = call.target_id,
                .evidence = {
                    {"reason", bind_res.reason},
                    {"call_text", call.call_text},
                    {"pos_args", call.pos_args},
                    {"kw_names", call.kw_names}
                }
            };
            std::string k = finding.diff_key();
            if (!seen_keys.contains(k)) {
              seen_keys.insert(k);
              report.counts[finding.code]++;
              report.findings.push_back(std::move(finding));
            }
          }
        }
      }
    }

    // 3. Check Missing Symbols in Calls (MISSING_SYMBOL)
    for (const auto& call : facts->calls) {
      // Case A: Module member calls (M.name(...))
      if (call.shape == lang::CalleeShape::Attr && !call.receiver.empty()) {
        auto it = imports_by_name.find(call.receiver);
        if (it != imports_by_name.end() && it->second.is_whole_module) {
          const auto* target_facts = store.get(it->second.target_file);
          if (target_facts) {
            bool sym_found = false;
            for (const auto& s : target_facts->symbols) {
              if (s.name == call.name || s.qualname == call.name) { sym_found = true; break; }
            }
            if (!sym_found) {
              for (const auto& mn : target_facts->module_names) {
                if (mn == call.name) { sym_found = true; break; }
              }
            }
            if (!sym_found) {
              for (const auto& ti : target_facts->imports) {
                for (const auto& [to, ta] : ti.names) {
                  if (ta == call.name || to == call.name) { sym_found = true; break; }
                }
                if (sym_found) break;
              }
            }
            if (!sym_found) {
              AuditFinding finding{
                  .code = "MISSING_SYMBOL",
                  .file = f,
                  .line = static_cast<int>(call.line),
                  .col = static_cast<int>(call.col),
                  .message = "Module '" + it->second.target_file + "' has no member '" + call.name + "'",
                  .symbol = call.caller_id,
                  .callee = it->second.target_file + "::" + call.name,
                  .evidence = {{"module", it->second.target_file}, {"member", call.name}, {"call_text", call.text}}
              };
              std::string k = finding.diff_key();
              if (!seen_keys.contains(k)) {
                seen_keys.insert(k);
                report.counts[finding.code]++;
                report.findings.push_back(std::move(finding));
              }
            }
          }
        }
      }

      // Case B: Self/Cls method calls (self.method(...))
      if (call.shape == lang::CalleeShape::SelfAttr || call.shape == lang::CalleeShape::ClsAttr) {
        const auto* caller_sym = store.find_symbol_by_id(call.caller_id);
        if (caller_sym && !caller_sym->class_qualname.empty()) {
          const auto* cls = facts->find(caller_sym->class_qualname);
          if (cls) {
            const auto* m = resolver.find_method_in_class_hierarchy(store, cls, call.name);
            if (!m) {
              AuditFinding finding{
                  .code = "MISSING_SYMBOL",
                  .file = f,
                  .line = static_cast<int>(call.line),
                  .col = static_cast<int>(call.col),
                  .message = "Class '" + cls->name + "' has no method '" + call.name + "'",
                  .symbol = call.caller_id,
                  .callee = cls->id + "::" + call.name,
                  .evidence = {{"class", cls->name}, {"method", call.name}, {"call_text", call.text}}
              };
              std::string k = finding.diff_key();
              if (!seen_keys.contains(k)) {
                seen_keys.insert(k);
                report.counts[finding.code]++;
                report.findings.push_back(std::move(finding));
              }
            }
          }
        }
      }

      // Case C: Super method calls (super().method(...))
      if (call.shape == lang::CalleeShape::SuperAttr) {
        const auto* caller_sym = store.find_symbol_by_id(call.caller_id);
        if (caller_sym && !caller_sym->class_qualname.empty()) {
          const auto* cls = facts->find(caller_sym->class_qualname);
          if (cls && !cls->bases.empty()) {
            const auto* base_cls = resolver.find_class_symbol(store, *facts, cls->bases[0]);
            if (base_cls) {
              const auto* m = resolver.find_method_in_class_hierarchy(store, base_cls, call.name);
              if (!m) {
                AuditFinding finding{
                    .code = "MISSING_SYMBOL",
                    .file = f,
                    .line = static_cast<int>(call.line),
                    .col = static_cast<int>(call.col),
                    .message = "Base class '" + base_cls->name + "' has no method '" + call.name + "'",
                    .symbol = call.caller_id,
                    .callee = base_cls->id + "::" + call.name,
                    .evidence = {{"base_class", base_cls->name}, {"method", call.name}, {"call_text", call.text}}
                };
                std::string k = finding.diff_key();
                if (!seen_keys.contains(k)) {
                  seen_keys.insert(k);
                  report.counts[finding.code]++;
                  report.findings.push_back(std::move(finding));
                }
              }
            }
          }
        }
      }
    }
  }

  // Sort findings deterministically by file, line, col, code
  std::sort(report.findings.begin(), report.findings.end(), [](const AuditFinding& a, const AuditFinding& b) {
    if (a.file != b.file) return a.file < b.file;
    if (a.line != b.line) return a.line < b.line;
    if (a.col != b.col) return a.col < b.col;
    return a.code < b.code;
  });

  return report;
}

AuditReport audit_workspace(
    const core::Config& config,
    const std::filesystem::path& root,
    const std::optional<std::filesystem::path>& base) {
  if (base.has_value()) {
    auto base_rep = audit_workspace(config, *base, std::nullopt);
    auto curr_rep = audit_workspace(config, root, std::nullopt);
    return diff_audit_reports(base_rep, curr_rep);
  }

  core::Config effective_config = config;
  effective_config.root = root;

  auto scanned = index::scan(effective_config, root);

  index::FactStore store(root);
  std::vector<AuditFinding> syntax_errors;

  for (const auto& item : scanned) {
    auto full_path = root / platform::from_utf8(item.rel);

    std::string content;
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs) continue;
    content.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    auto lang_info = lang::LanguageRegistry::get().for_path(item.rel);
    if (lang_info.lang == lang::Language::Unknown) continue;

    // Check syntax errors
    auto tree = lang::parse_string(lang_info.lang, content);
    if (tree) {
      TSNode root_node = ts_tree_root_node(tree.get());
      auto errs = lang::collect_syntax_errors(root_node, content, 50);
      for (const auto& err : errs) {
        AuditFinding f{
            .code = "SYNTAX_ERROR",
            .file = item.rel,
            .line = static_cast<int>(err.line),
            .col = static_cast<int>(err.col),
            .message = "Syntax error in " + item.rel + " at " + std::to_string(err.line) + ":" + std::to_string(err.col) +
                       (err.missing ? (": missing '" + err.expected + "'") : ": unexpected token"),
            .symbol = item.rel,
            .callee = "",
            .evidence = {{"line", err.line}, {"col", err.col}, {"missing", err.missing}, {"expected", err.expected}}
        };
        syntax_errors.push_back(std::move(f));
      }
    }

    // Extract facts
    auto facts = lang::extract_any(item.rel, content, lang_info.lang);
    facts.mtime_ns = item.mtime_ns;
    facts.size = item.size;
    store.put(std::move(facts));
  }

  return audit_fact_store(effective_config, store, syntax_errors);
}

} // namespace gs::gate
