#include "gate/rules.hpp"

#include <fstream>
#include <regex>
#include "core/paths.hpp"
#include "lang/language.hpp"
#include "lang/parser.hpp"

namespace gs::gate {

namespace {

bool ranges_intersect(uint32_t s1, uint32_t e1, uint32_t s2, uint32_t e2) {
  return !(e1 <= s2 || e2 <= s1);
}

bool node_in_modified_ranges(TSNode node, const std::vector<lang::ByteRange>& ranges) {
  if (ranges.empty()) return true; // if no ranges given, inspect all
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  for (const auto& r : ranges) {
    if (ranges_intersect(start, end, r.start, r.end)) {
      return true;
    }
  }
  return false;
}

std::string get_node_text(TSNode node, std::string_view source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start < source.size() && end <= source.size() && start <= end) {
    return std::string(source.substr(start, end - start));
  }
  return "";
}

Location get_node_location(const std::string& file, TSNode node) {
  TSPoint pt = ts_node_start_point(node);
  return Location{file, static_cast<int>(pt.row + 1), static_cast<int>(pt.column + 1)};
}

Severity parse_severity(std::string_view s) {
  if (s == "error") return Severity::Error;
  if (s == "info")  return Severity::Info;
  return Severity::Warn;
}

} // namespace

RulesEngine::RulesEngine() {
  init_defaults();
}

RulesEngine::RulesEngine(const std::filesystem::path& root) {
  init_defaults();
  load_config(root);
}

RulesEngine::~RulesEngine() = default;

void RulesEngine::init_defaults() {
  rules_["max-function-lines"] = RuleConfig{.id = "max-function-lines", .severity = "warn", .max = 80};
  rules_["max-complexity"]     = RuleConfig{.id = "max-complexity",     .severity = "warn", .max = 12};
  rules_["max-nesting"]        = RuleConfig{.id = "max-nesting",        .severity = "warn", .max = 4};
  rules_["max-params"]         = RuleConfig{.id = "max-params",         .severity = "warn", .max = 6};
  rules_["py-mutable-default-arg"] = RuleConfig{.id = "py-mutable-default-arg", .severity = "error"};
  rules_["py-bare-except"]     = RuleConfig{.id = "py-bare-except",     .severity = "error"};
  rules_["py-public-type-hints"] = RuleConfig{.id = "py-public-type-hints", .severity = "off"};
  rules_["py-public-docstring"]  = RuleConfig{.id = "py-public-docstring",  .severity = "off", .min_lines = 10};
  rules_["no-debug-print"]     = RuleConfig{
      .id = "no-debug-print",
      .severity = "warn",
      .exclude_paths = {"**/test*/**", "**/*_test.py", "**/*.test.*", "**/*.spec.*", "scripts/**"}
  };
  rules_["ts-no-explicit-any"] = RuleConfig{.id = "ts-no-explicit-any", .severity = "warn"};
  rules_["no-empty-catch"]     = RuleConfig{.id = "no-empty-catch",     .severity = "warn"};
}

void RulesEngine::load_config(const std::filesystem::path& root) {
  std::filesystem::path rules_file = root / ".graphstate" / "rules.json";
  std::error_code ec;
  if (!std::filesystem::exists(rules_file, ec)) {
    return;
  }

  try {
    std::ifstream in(rules_file);
    if (!in.is_open()) return;
    nlohmann::json j = nlohmann::json::parse(in);
    in.close();

    // 1. Built-in overrides
    if (j.contains("rules") && j["rules"].is_object()) {
      for (auto it = j["rules"].begin(); it != j["rules"].end(); ++it) {
        std::string rid = it.key();
        auto& cfg = rules_[rid];
        cfg.id = rid;
        if (it.value().is_object()) {
          const auto& ro = it.value();
          if (ro.contains("severity") && ro["severity"].is_string()) {
            cfg.severity = ro["severity"].get<std::string>();
          }
          if (ro.contains("max") && ro["max"].is_number_integer()) {
            cfg.max = ro["max"].get<int>();
          }
          if (ro.contains("min_lines") && ro["min_lines"].is_number_integer()) {
            cfg.min_lines = ro["min_lines"].get<int>();
          }
        }
      }
    }

    // 2. Custom rules
    if (j.contains("custom") && j["custom"].is_array()) {
      for (const auto& cr : j["custom"]) {
        if (!cr.contains("id") || !cr.contains("language") || !cr.contains("query")) {
          continue;
        }

        std::string lang_name = cr["language"].get<std::string>();
        const auto& lang_info = lang::LanguageRegistry::get().for_name(lang_name);
        if (!lang_info.ts) {
          rule_errors_.push_back("Custom rule '" + cr["id"].get<std::string>() + "' has unknown language: " + lang_name);
          continue;
        }

        CustomRule rule;
        rule.id = cr["id"].get<std::string>();
        rule.language = lang_info.lang;
        rule.severity = parse_severity(cr.value("severity", "error"));
        rule.query_str = cr["query"].get<std::string>();
        rule.message = cr.value("message", "Custom rule violation");
        rule.fix_hint = cr.value("fix_hint", "Fix according to project guidelines");

        if (cr.contains("paths") && cr["paths"].is_array()) {
          for (const auto& p : cr["paths"]) {
            if (p.is_string()) rule.paths.push_back(p.get<std::string>());
          }
        }

        uint32_t error_offset = 0;
        TSQueryError error_type = TSQueryErrorNone;
        rule.query.reset(ts_query_new(lang_info.ts, rule.query_str.data(),
                                      static_cast<uint32_t>(rule.query_str.size()),
                                      &error_offset, &error_type));

        if (!rule.query) {
          rule_errors_.push_back("Custom rule '" + rule.id + "' failed to compile query at offset " +
                                 std::to_string(error_offset));
          continue;
        }

        if (cr.contains("unless_query") && cr["unless_query"].is_string()) {
          rule.unless_query_str = cr["unless_query"].get<std::string>();
          rule.unless_query.reset(ts_query_new(lang_info.ts, rule.unless_query_str.data(),
                                               static_cast<uint32_t>(rule.unless_query_str.size()),
                                               &error_offset, &error_type));
        }

        custom_rules_.push_back(std::move(rule));
      }
    }
  } catch (const std::exception& ex) {
    rule_errors_.push_back(std::string("Error parsing rules.json: ") + ex.what());
  }
}

bool RulesEngine::is_path_excluded(const std::string& file, const std::vector<std::string>& patterns) const {
  for (const auto& pat : patterns) {
    if (core::glob_match(pat, file, false)) {
      return true;
    }
  }
  return false;
}

std::vector<Violation> RulesEngine::check(const std::string& file,
                                         lang::Language lang,
                                         std::string_view post_source,
                                         const lang::FileFacts& post_facts,
                                         const std::vector<lang::ByteRange>& modified_ranges) const {
  std::vector<Violation> violations;

  // 1. Symbol metric checks from FileFacts
  for (const auto& sym : post_facts.symbols) {
    if (sym.kind == lang::SymbolKind::Module) continue;

    bool touched = false;
    if (modified_ranges.empty()) {
      touched = true;
    } else {
      for (const auto& r : modified_ranges) {
        if (ranges_intersect(sym.def_range.start, sym.def_range.end, r.start, r.end)) {
          touched = true;
          break;
        }
      }
    }
    if (!touched) continue;

    Location loc{file, static_cast<int>(sym.lines.start), static_cast<int>(sym.indent_col + 1)};

    // max-function-lines
    auto it_lines = rules_.find("max-function-lines");
    if (it_lines != rules_.end() && it_lines->second.severity != "off" && it_lines->second.max > 0) {
      int loc_lines = static_cast<int>(sym.lines.end - sym.lines.start + 1);
      if (loc_lines > it_lines->second.max) {
        violations.push_back(Violation{
            .code = "RULE_VIOLATION",
            .severity = parse_severity(it_lines->second.severity),
            .phase = "QUALITY",
            .message = sym.name + " is " + std::to_string(loc_lines) + " lines (max " + std::to_string(it_lines->second.max) + ").",
            .symbol_id = sym.id,
            .location = loc,
            .evidence = {{"value", loc_lines}, {"max", it_lines->second.max}},
            .fix_hint = "Extract helpers.",
            .rule_id = "max-function-lines"
        });
      }
    }

    // max-complexity
    auto it_comp = rules_.find("max-complexity");
    if (it_comp != rules_.end() && it_comp->second.severity != "off" && it_comp->second.max > 0) {
      if (sym.complexity > it_comp->second.max) {
        violations.push_back(Violation{
            .code = "RULE_VIOLATION",
            .severity = parse_severity(it_comp->second.severity),
            .phase = "QUALITY",
            .message = sym.name + " has complexity " + std::to_string(sym.complexity) + " (max " + std::to_string(it_comp->second.max) + ").",
            .symbol_id = sym.id,
            .location = loc,
            .evidence = {{"value", sym.complexity}, {"max", it_comp->second.max}},
            .fix_hint = "Simplify control flow or extract helper functions.",
            .rule_id = "max-complexity"
        });
      }
    }

    // max-nesting
    auto it_nest = rules_.find("max-nesting");
    if (it_nest != rules_.end() && it_nest->second.severity != "off" && it_nest->second.max > 0) {
      if (sym.max_nesting > it_nest->second.max) {
        violations.push_back(Violation{
            .code = "RULE_VIOLATION",
            .severity = parse_severity(it_nest->second.severity),
            .phase = "QUALITY",
            .message = sym.name + " has nesting depth " + std::to_string(sym.max_nesting) + " (max " + std::to_string(it_nest->second.max) + ").",
            .symbol_id = sym.id,
            .location = loc,
            .evidence = {{"value", sym.max_nesting}, {"max", it_nest->second.max}},
            .fix_hint = "Invert conditions to return early or extract helper functions.",
            .rule_id = "max-nesting"
        });
      }
    }

    // max-params
    auto it_params = rules_.find("max-params");
    if (it_params != rules_.end() && it_params->second.severity != "off" && it_params->second.max > 0) {
      int param_count = 0;
      for (const auto& p : sym.sig.params) {
        if (p.name != "self" && p.name != "cls" && p.name != "this") {
          ++param_count;
        }
      }
      if (param_count > it_params->second.max) {
        violations.push_back(Violation{
            .code = "RULE_VIOLATION",
            .severity = parse_severity(it_params->second.severity),
            .phase = "QUALITY",
            .message = sym.name + " has " + std::to_string(param_count) + " parameters (max " + std::to_string(it_params->second.max) + ").",
            .symbol_id = sym.id,
            .location = loc,
            .evidence = {{"value", param_count}, {"max", it_params->second.max}},
            .fix_hint = "Group parameters into an options object / dataclass.",
            .rule_id = "max-params"
        });
      }
    }
  }

  // 2. Tree-sitter AST queries
  lang::TreePtr tree = lang::parse_string(lang, post_source);
  if (!tree) return violations;

  TSNode root_node = ts_tree_root_node(tree.get());

  // AST Visitor helper
  std::function<void(TSNode)> walk = [&](TSNode node) {
    if (!node_in_modified_ranges(node, modified_ranges)) {
      return;
    }

    std::string_view type = ts_node_type(node);

    // Python: py-mutable-default-arg
    if (lang == lang::Language::Python) {
      auto it_mut = rules_.find("py-mutable-default-arg");
      if (it_mut != rules_.end() && it_mut->second.severity != "off") {
        if (type == "default_parameter" || type == "typed_default_parameter") {
          TSNode val_node = ts_node_child_by_field_name(node, "value", 5);
          if (ts_node_is_null(val_node)) {
            // Find child after '='
            for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
              TSNode c = ts_node_child(node, i);
              if (std::string_view(ts_node_type(c)) == "=" && i + 1 < ts_node_child_count(node)) {
                val_node = ts_node_child(node, i + 1);
                break;
              }
            }
          }

          if (!ts_node_is_null(val_node)) {
            std::string_view val_type = ts_node_type(val_node);
            bool is_mutable = false;
            if (val_type == "list" || val_type == "dictionary" || val_type == "set") {
              is_mutable = true;
            } else if (val_type == "call") {
              TSNode fn_node = ts_node_child_by_field_name(val_node, "function", 8);
              if (ts_node_is_null(fn_node)) fn_node = ts_node_child(val_node, 0);
              if (!ts_node_is_null(fn_node)) {
                std::string fn_name = get_node_text(fn_node, post_source);
                if (fn_name == "list" || fn_name == "dict" || fn_name == "set") {
                  is_mutable = true;
                }
              }
            }

            if (is_mutable) {
              TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
              if (ts_node_is_null(name_node)) name_node = ts_node_child(node, 0);
              std::string p_name = ts_node_is_null(name_node) ? "arg" : get_node_text(name_node, post_source);
              violations.push_back(Violation{
                  .code = "RULE_VIOLATION",
                  .severity = parse_severity(it_mut->second.severity),
                  .phase = "QUALITY",
                  .message = "Mutable default argument in '" + p_name + "'. Use None and initialize in body.",
                  .location = get_node_location(file, val_node),
                  .evidence = {{"param", p_name}, {"type", std::string(val_type)}},
                  .fix_hint = "Change default to None and add 'if " + p_name + " is None: " + p_name + " = []'.",
                  .rule_id = "py-mutable-default-arg"
              });
            }
          }
        }
      }

      // Python: py-bare-except
      auto it_bare = rules_.find("py-bare-except");
      if (it_bare != rules_.end() && it_bare->second.severity != "off") {
        if (type == "except_clause") {
          TSNode val_node = ts_node_child_by_field_name(node, "value", 5);
          bool has_val = !ts_node_is_null(val_node);
          if (!has_val) {
            // Also check child types before colon/block
            for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
              std::string_view ctype = ts_node_type(ts_node_child(node, i));
              if (ctype != "except" && ctype != ":" && ctype != "block" && ctype != "\n" && ctype != "comment") {
                has_val = true;
                break;
              }
            }
          }
          if (!has_val) {
            violations.push_back(Violation{
                .code = "RULE_VIOLATION",
                .severity = parse_severity(it_bare->second.severity),
                .phase = "QUALITY",
                .message = "Bare except clause catches SystemExit and KeyboardInterrupt.",
                .location = get_node_location(file, node),
                .evidence = {{"clause", get_node_text(node, post_source)}},
                .fix_hint = "Use 'except Exception:' to catch runtime exceptions safely.",
                .rule_id = "py-bare-except"
            });
          }
        }
      }

      // Python: no-empty-catch
      auto it_ec = rules_.find("no-empty-catch");
      if (it_ec != rules_.end() && it_ec->second.severity != "off") {
        if (type == "except_clause") {
          TSNode block_node = ts_node_child_by_field_name(node, "body", 4);
          if (ts_node_is_null(block_node)) {
            for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
              TSNode c = ts_node_child(node, i);
              if (std::string_view(ts_node_type(c)) == "block") {
                block_node = c;
                break;
              }
            }
          }
          if (!ts_node_is_null(block_node)) {
            uint32_t named_count = ts_node_named_child_count(block_node);
            bool is_empty = false;
            if (named_count == 0) {
              is_empty = true;
            } else if (named_count == 1) {
              TSNode stmt = ts_node_named_child(block_node, 0);
              std::string_view stype = ts_node_type(stmt);
              std::string text = get_node_text(stmt, post_source);
              if (stype == "pass_statement" || text == "pass" || text == "...") {
                is_empty = true;
              }
            }
            if (is_empty) {
              violations.push_back(Violation{
                  .code = "RULE_VIOLATION",
                  .severity = parse_severity(it_ec->second.severity),
                  .phase = "QUALITY",
                  .message = "Empty catch block silently suppresses errors.",
                  .location = get_node_location(file, block_node),
                  .evidence = {{"body", "pass"}},
                  .fix_hint = "Log the error or re-throw.",
                  .rule_id = "no-empty-catch"
              });
            }
          }
        }
      }
    }

    // TS/JS: ts-no-explicit-any
    if (lang == lang::Language::TypeScript || lang == lang::Language::Tsx) {
      auto it_any = rules_.find("ts-no-explicit-any");
      if (it_any != rules_.end() && it_any->second.severity != "off") {
        if (type == "predefined_type" && get_node_text(node, post_source) == "any") {
          violations.push_back(Violation{
              .code = "RULE_VIOLATION",
              .severity = parse_severity(it_any->second.severity),
              .phase = "QUALITY",
              .message = "Explicit 'any' type loses type safety.",
              .location = get_node_location(file, node),
              .evidence = {{"type", "any"}},
              .fix_hint = "Use 'unknown' or specify a more precise type.",
              .rule_id = "ts-no-explicit-any"
          });
        }
      }
    }

    // TS/JS: no-empty-catch
    if (lang == lang::Language::TypeScript || lang == lang::Language::Tsx || lang == lang::Language::JavaScript) {
      auto it_ec = rules_.find("no-empty-catch");
      if (it_ec != rules_.end() && it_ec->second.severity != "off") {
        if (type == "catch_clause") {
          TSNode body = ts_node_child_by_field_name(node, "body", 4);
          if (!ts_node_is_null(body) && std::string_view(ts_node_type(body)) == "statement_block") {
            // Check if statement block has no actual statement children (only '{' and '}')
            uint32_t stmt_count = 0;
            for (uint32_t i = 0; i < ts_node_child_count(body); ++i) {
              std::string_view ctype = ts_node_type(ts_node_child(body, i));
              if (ctype != "{" && ctype != "}") {
                ++stmt_count;
              }
            }
            if (stmt_count == 0) {
              violations.push_back(Violation{
                  .code = "RULE_VIOLATION",
                  .severity = parse_severity(it_ec->second.severity),
                  .phase = "QUALITY",
                  .message = "Empty catch block silently suppresses errors.",
                  .location = get_node_location(file, body),
                  .evidence = {{"body", "{}"}},
                  .fix_hint = "Log the error or re-throw.",
                  .rule_id = "no-empty-catch"
              });
            }
          }
        }
      }
    }

    // no-debug-print
    auto it_print = rules_.find("no-debug-print");
    if (it_print != rules_.end() && it_print->second.severity != "off") {
      if (!is_path_excluded(file, it_print->second.exclude_paths)) {
        if (lang == lang::Language::Python && type == "call") {
          TSNode fn_node = ts_node_child_by_field_name(node, "function", 8);
          if (ts_node_is_null(fn_node)) fn_node = ts_node_child(node, 0);
          if (!ts_node_is_null(fn_node) && get_node_text(fn_node, post_source) == "print") {
            violations.push_back(Violation{
                .code = "RULE_VIOLATION",
                .severity = parse_severity(it_print->second.severity),
                .phase = "QUALITY",
                .message = "Debug print left in production code.",
                .location = get_node_location(file, node),
                .evidence = {{"snippet", get_node_text(node, post_source)}},
                .fix_hint = "Remove print call or use a proper logger.",
                .rule_id = "no-debug-print"
            });
          }
        } else if ((lang == lang::Language::TypeScript || lang == lang::Language::Tsx || lang == lang::Language::JavaScript) &&
                   type == "call_expression") {
          TSNode fn_node = ts_node_child_by_field_name(node, "function", 8);
          if (ts_node_is_null(fn_node)) fn_node = ts_node_child(node, 0);
          if (!ts_node_is_null(fn_node) && std::string_view(ts_node_type(fn_node)) == "member_expression") {
            TSNode obj_node = ts_node_child_by_field_name(fn_node, "object", 6);
            TSNode prop_node = ts_node_child_by_field_name(fn_node, "property", 8);
            if (!ts_node_is_null(obj_node) && !ts_node_is_null(prop_node)) {
              if (get_node_text(obj_node, post_source) == "console") {
                std::string prop = get_node_text(prop_node, post_source);
                if (prop == "log" || prop == "debug") {
                  violations.push_back(Violation{
                      .code = "RULE_VIOLATION",
                      .severity = parse_severity(it_print->second.severity),
                      .phase = "QUALITY",
                      .message = "Debug print left in production code.",
                      .location = get_node_location(file, node),
                      .evidence = {{"snippet", get_node_text(node, post_source)}},
                      .fix_hint = "Remove console.log or use a proper logger.",
                      .rule_id = "no-debug-print"
                  });
                }
              }
            }
          }
        }
      }
    }

    // Recurse children
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
      walk(ts_node_child(node, i));
    }
  };

  walk(root_node);

  // 3. Custom Rules Execution
  for (const auto& cr : custom_rules_) {
    if (cr.language != lang) continue;
    if (!cr.paths.empty() && !is_path_excluded(file, cr.paths)) {
      // If paths given, file must match at least one
      bool path_matched = false;
      for (const auto& p : cr.paths) {
        if (core::glob_match(p, file, false)) {
          path_matched = true;
          break;
        }
      }
      if (!path_matched) continue;
    }

    lang::QueryCursorPtr cursor(ts_query_cursor_new());
    ts_query_cursor_exec(cursor.get(), cr.query.get(), root_node);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor.get(), &match)) {
      // Evaluate predicates for this pattern
      uint32_t step_count = 0;
      const TSQueryPredicateStep* steps = ts_query_predicates_for_pattern(cr.query.get(), match.pattern_index, &step_count);
      bool predicates_passed = true;

      // Group steps into predicates
      uint32_t step_idx = 0;
      while (step_idx < step_count) {
        if (steps[step_idx].type == TSQueryPredicateStepTypeDone) {
          ++step_idx;
          continue;
        }

        // Predicate name
        uint32_t name_len = 0;
        const char* pred_name = ts_query_string_value_for_id(cr.query.get(), steps[step_idx].value_id, &name_len);
        std::string pred(pred_name, name_len);
        ++step_idx;

        std::vector<TSQueryPredicateStep> args;
        while (step_idx < step_count && steps[step_idx].type != TSQueryPredicateStepTypeDone) {
          args.push_back(steps[step_idx]);
          ++step_idx;
        }
        if (step_idx < step_count && steps[step_idx].type == TSQueryPredicateStepTypeDone) {
          ++step_idx;
        }

        auto get_capture_text = [&](uint32_t capture_id) -> std::string {
          for (uint16_t c = 0; c < match.capture_count; ++c) {
            if (match.captures[c].index == capture_id) {
              return get_node_text(match.captures[c].node, post_source);
            }
          }
          return "";
        };

        if (pred == "eq?" && args.size() >= 2) {
          std::string text = get_capture_text(args[0].value_id);
          uint32_t l = 0;
          std::string val = ts_query_string_value_for_id(cr.query.get(), args[1].value_id, &l);
          if (text != val) { predicates_passed = false; break; }
        } else if (pred == "not-eq?" && args.size() >= 2) {
          std::string text = get_capture_text(args[0].value_id);
          uint32_t l = 0;
          std::string val = ts_query_string_value_for_id(cr.query.get(), args[1].value_id, &l);
          if (text == val) { predicates_passed = false; break; }
        } else if (pred == "match?" && args.size() >= 2) {
          std::string text = get_capture_text(args[0].value_id);
          uint32_t l = 0;
          std::string re_str = ts_query_string_value_for_id(cr.query.get(), args[1].value_id, &l);
          try {
            std::regex re(re_str, std::regex::ECMAScript | std::regex::optimize);
            if (!std::regex_search(text, re)) { predicates_passed = false; break; }
          } catch (...) { predicates_passed = false; break; }
        } else if (pred == "not-match?" && args.size() >= 2) {
          std::string text = get_capture_text(args[0].value_id);
          uint32_t l = 0;
          std::string re_str = ts_query_string_value_for_id(cr.query.get(), args[1].value_id, &l);
          try {
            std::regex re(re_str, std::regex::ECMAScript | std::regex::optimize);
            if (std::regex_search(text, re)) { predicates_passed = false; break; }
          } catch (...) { predicates_passed = false; break; }
        } else if (pred == "any-of?" && args.size() >= 2) {
          std::string text = get_capture_text(args[0].value_id);
          bool matched = false;
          for (size_t a = 1; a < args.size(); ++a) {
            uint32_t l = 0;
            std::string val = ts_query_string_value_for_id(cr.query.get(), args[a].value_id, &l);
            if (text == val) { matched = true; break; }
          }
          if (!matched) { predicates_passed = false; break; }
        }
      }

      if (!predicates_passed) continue;

      // Find @hit node
      TSNode hit_node = match.captures[0].node;
      for (uint16_t c = 0; c < match.capture_count; ++c) {
        uint32_t cap_len = 0;
        const char* cap_name = ts_query_capture_name_for_id(cr.query.get(), match.captures[c].index, &cap_len);
        if (std::string_view(cap_name, cap_len) == "hit") {
          hit_node = match.captures[c].node;
          break;
        }
      }

      if (!node_in_modified_ranges(hit_node, modified_ranges)) {
        continue;
      }

      // If unless_query specified, test if it matches inside hit_node
      if (cr.unless_query) {
        lang::QueryCursorPtr unless_cursor(ts_query_cursor_new());
        ts_query_cursor_exec(unless_cursor.get(), cr.unless_query.get(), hit_node);
        TSQueryMatch unless_match;
        if (ts_query_cursor_next_match(unless_cursor.get(), &unless_match)) {
          // Suppressed by unless_query!
          continue;
        }
      }

      violations.push_back(Violation{
          .code = "RULE_VIOLATION",
          .severity = cr.severity,
          .phase = "QUALITY",
          .message = cr.message,
          .location = get_node_location(file, hit_node),
          .evidence = {{"snippet", get_node_text(hit_node, post_source)}},
          .fix_hint = cr.fix_hint,
          .rule_id = cr.id
      });
    }
  }

  return violations;
}

} // namespace gs::gate
