#include "lang/ts_common.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace gs::lang::ts {

std::string_view node_text(TSNode node, std::string_view src) {
  if (ts_node_is_null(node)) return "";
  uint32_t sb = ts_node_start_byte(node);
  uint32_t eb = ts_node_end_byte(node);
  if (sb >= src.size() || eb > src.size() || sb > eb) return "";
  return src.substr(sb, eb - sb);
}

std::string_view trim_str(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

std::string strip_quotes(std::string_view s) {
  s = trim_str(s);
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\'') ||
                        (s.front() == '`' && s.back() == '`'))) {
    s = s.substr(1, s.size() - 2);
  }
  return std::string(s);
}

std::string extract_jsdoc(TSNode node, std::string_view src) {
  TSNode prev = ts_node_prev_sibling(node);
  if (!ts_node_is_null(prev) && std::string_view(ts_node_type(prev)) == "comment") {
    std::string_view ctext = node_text(prev, src);
    if (ctext.starts_with("/**") && ctext.ends_with("*/")) {
      ctext.remove_prefix(3);
      ctext.remove_suffix(2);
      size_t nl = ctext.find_first_of("\r\n");
      if (nl != std::string_view::npos) {
        ctext = ctext.substr(0, nl);
      }
      std::string_view doc = trim_str(ctext);
      while (doc.starts_with("*")) {
        doc = trim_str(doc.substr(1));
      }
      if (doc.size() > 160) {
        doc = doc.substr(0, 160);
      }
      return std::string(doc);
    }
  }
  return "";
}

void parse_single_param(TSNode node, std::vector<Param>& out, std::string_view src, bool is_typescript) {
  std::string_view type = ts_node_type(node);

  if (type == "," || type == "(" || type == ")") {
    return;
  }

  Param p;

  if (type == "identifier") {
    p.name = std::string(node_text(node, src));
  } else if (type == "required_parameter" || type == "optional_parameter") {
    if (type == "optional_parameter") {
      p.optional = true;
    }
    TSNode pat = ts_node_child_by_field_name(node, "pattern", 7);
    if (!ts_node_is_null(pat)) {
      std::string_view pat_type = ts_node_type(pat);
      if (pat_type == "identifier") {
        std::string_view id_txt = node_text(pat, src);
        if (id_txt == "this") {
          return; // Skip 'this' parameter entirely
        }
        p.name = std::string(id_txt);
      } else if (pat_type == "rest_pattern") {
        p.kind = ParamKind::VarPositional;
        TSNode inner = ts_node_named_child(pat, 0);
        p.name = std::string(node_text(inner, src));
      } else if (pat_type == "object_pattern" || pat_type == "array_pattern") {
        p.name = "<destructured>";
      }
    } else {
      TSNode inner = ts_node_named_child(node, 0);
      if (!ts_node_is_null(inner)) {
        p.name = std::string(node_text(inner, src));
      }
    }

    TSNode val = ts_node_child_by_field_name(node, "value", 5);
    if (!ts_node_is_null(val)) {
      p.has_default = true;
    }

    TSNode tnode = ts_node_child_by_field_name(node, "type", 4);
    if (!ts_node_is_null(tnode)) {
      std::string_view ttext = node_text(tnode, src);
      if (ttext.starts_with(":")) {
        ttext.remove_prefix(1);
      }
      p.type = std::string(trim_str(ttext));
    }
  } else if (type == "assignment_pattern") {
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    p.has_default = true;
    if (!ts_node_is_null(left)) {
      std::string_view ltype = ts_node_type(left);
      if (ltype == "identifier") {
        p.name = std::string(node_text(left, src));
      } else if (ltype == "object_pattern" || ltype == "array_pattern") {
        p.name = "<destructured>";
      }
    }
  } else if (type == "rest_pattern") {
    p.kind = ParamKind::VarPositional;
    TSNode inner = ts_node_named_child(node, 0);
    p.name = std::string(node_text(inner, src));
  } else if (type == "object_pattern" || type == "array_pattern") {
    p.name = "<destructured>";
  }

  (void)is_typescript;
  if (!p.name.empty()) {
    out.push_back(std::move(p));
  }
}

void parse_parameters(TSNode params_node, std::vector<Param>& out, std::string_view src, bool is_typescript) {
  if (ts_node_is_null(params_node)) return;
  std::string_view ptype = ts_node_type(params_node);

  if (ptype == "identifier") {
    // Arrow function with bare parameter
    Param p;
    p.name = std::string(node_text(params_node, src));
    out.push_back(std::move(p));
    return;
  }

  uint32_t count = ts_node_named_child_count(params_node);
  for (uint32_t i = 0; i < count; ++i) {
    parse_single_param(ts_node_named_child(params_node, i), out, src, is_typescript);
  }
}

void extract_call(TSNode call_node, const std::string& caller_id, std::vector<CallFact>& calls, std::string_view src) {
  CallFact cf;
  cf.caller_id = caller_id;
  TSPoint pt = ts_node_start_point(call_node);
  cf.line = pt.row + 1;
  cf.col = pt.column + 1;
  cf.start_byte = ts_node_start_byte(call_node);

  std::string_view full_text = node_text(call_node, src);
  std::string sline;
  for (char c : full_text) {
    if (c == '\r' || c == '\n') {
      if (!sline.empty() && sline.back() != ' ') sline += ' ';
    } else {
      sline += c;
    }
    if (sline.size() >= 120) {
      sline = sline.substr(0, 117) + "...";
      break;
    }
  }
  cf.text = std::string(trim_str(sline));

  TSNode fn_node = ts_node_child_by_field_name(call_node, "function", 8);
  if (!ts_node_is_null(fn_node)) {
    std::string_view fn_type = ts_node_type(fn_node);
    if (fn_type == "identifier") {
      cf.shape = CalleeShape::Name;
      cf.name = std::string(node_text(fn_node, src));
    } else if (fn_type == "member_expression") {
      TSNode prop = ts_node_child_by_field_name(fn_node, "property", 8);
      TSNode obj = ts_node_child_by_field_name(fn_node, "object", 6);
      cf.name = std::string(node_text(prop, src));
      std::string_view obj_txt = node_text(obj, src);

      if (obj_txt == "this") {
        cf.shape = CalleeShape::SelfAttr;
      } else if (obj_txt == "super") {
        cf.shape = CalleeShape::SuperAttr;
      } else {
        cf.shape = CalleeShape::Attr;
        cf.receiver = std::string(obj_txt);
      }
    } else {
      cf.shape = CalleeShape::Other;
    }
  }

  TSNode args_node = ts_node_child_by_field_name(call_node, "arguments", 9);
  if (!ts_node_is_null(args_node)) {
    uint32_t count = ts_node_named_child_count(args_node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode arg = ts_node_named_child(args_node, i);
      if (std::string_view(ts_node_type(arg)) == "spread_element") {
        cf.has_star = true;
      } else {
        cf.pos_args++;
      }
    }
  }

  calls.push_back(std::move(cf));
}

void extract_new(TSNode new_node, const std::string& caller_id, std::vector<CallFact>& calls, std::string_view src) {
  CallFact cf;
  cf.caller_id = caller_id;
  cf.shape = CalleeShape::New;
  TSPoint pt = ts_node_start_point(new_node);
  cf.line = pt.row + 1;
  cf.col = pt.column + 1;
  cf.start_byte = ts_node_start_byte(new_node);

  std::string_view full_text = node_text(new_node, src);
  std::string sline;
  for (char c : full_text) {
    if (c == '\r' || c == '\n') {
      if (!sline.empty() && sline.back() != ' ') sline += ' ';
    } else {
      sline += c;
    }
    if (sline.size() >= 120) {
      sline = sline.substr(0, 117) + "...";
      break;
    }
  }
  cf.text = std::string(trim_str(sline));

  TSNode ctor_node = ts_node_child_by_field_name(new_node, "constructor", 11);
  if (!ts_node_is_null(ctor_node)) {
    std::string_view ctype = ts_node_type(ctor_node);
    if (ctype == "identifier") {
      cf.name = std::string(node_text(ctor_node, src));
    } else if (ctype == "member_expression") {
      TSNode prop = ts_node_child_by_field_name(ctor_node, "property", 8);
      TSNode obj = ts_node_child_by_field_name(ctor_node, "object", 6);
      cf.name = std::string(node_text(prop, src));
      cf.receiver = std::string(node_text(obj, src));
    }
  }

  TSNode args_node = ts_node_child_by_field_name(new_node, "arguments", 9);
  if (!ts_node_is_null(args_node)) {
    uint32_t count = ts_node_named_child_count(args_node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode arg = ts_node_named_child(args_node, i);
      if (std::string_view(ts_node_type(arg)) == "spread_element") {
        cf.has_star = true;
      } else {
        cf.pos_args++;
      }
    }
  }

  calls.push_back(std::move(cf));
}

void collect_calls(TSNode node, const std::string& caller_id, std::vector<CallFact>& calls, std::string_view src) {
  if (ts_node_is_null(node)) return;

  std::string_view type = ts_node_type(node);
  if (type == "class_declaration" || type == "class") {
    return; // Nested classes have their own method symbols
  }

  if (type == "call_expression") {
    extract_call(node, caller_id, calls, src);
  } else if (type == "new_expression") {
    extract_new(node, caller_id, calls, src);
  }

  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_calls(ts_node_child(node, i), caller_id, calls, src);
  }
}

void extract_import(TSNode import_node, std::vector<ImportFact>& imports, std::string_view src) {
  uint32_t line = ts_node_start_point(import_node).row + 1;
  std::string spec;
  TSNode src_node = ts_node_child_by_field_name(import_node, "source", 6);
  if (!ts_node_is_null(src_node)) {
    spec = strip_quotes(node_text(src_node, src));
  }

  // Check 'type' keyword child for 'import type'
  bool is_type_only = false;
  uint32_t count = ts_node_child_count(import_node);
  for (uint32_t i = 0; i < count; ++i) {
    if (node_text(ts_node_child(import_node, i), src) == "type") {
      is_type_only = true;
      break;
    }
  }

  TSNode clause = ts_node_child_by_field_name(import_node, "import_clause", 13);
  if (ts_node_is_null(clause)) {
    // Look for named child
    uint32_t ncount = ts_node_named_child_count(import_node);
    for (uint32_t i = 0; i < ncount; ++i) {
      TSNode c = ts_node_named_child(import_node, i);
      if (std::string_view(ts_node_type(c)) == "import_clause") {
        clause = c;
        break;
      }
    }
  }

  if (ts_node_is_null(clause)) {
    // Side effect import: import './styles.css'
    ImportFact imp;
    imp.kind = ImportFact::TsSideEffect;
    imp.spec = std::move(spec);
    imp.line = line;
    imp.is_type_only = is_type_only;
    imports.push_back(std::move(imp));
  } else {
    uint32_t ccount = ts_node_named_child_count(clause);
    for (uint32_t i = 0; i < ccount; ++i) {
      TSNode c = ts_node_named_child(clause, i);
      std::string_view ctype = ts_node_type(c);
      if (ctype == "identifier") {
        ImportFact imp;
        imp.kind = ImportFact::TsDefault;
        imp.spec = spec;
        imp.line = line;
        imp.is_type_only = is_type_only;
        std::string name = std::string(node_text(c, src));
        imp.names.emplace_back(name, name);
        imports.push_back(std::move(imp));
      } else if (ctype == "named_imports") {
        ImportFact imp;
        imp.kind = ImportFact::TsNamed;
        imp.spec = spec;
        imp.line = line;
        imp.is_type_only = is_type_only;
        uint32_t scount = ts_node_named_child_count(c);
        for (uint32_t si = 0; si < scount; ++si) {
          TSNode spec_node = ts_node_named_child(c, si);
          TSNode n_node = ts_node_child_by_field_name(spec_node, "name", 4);
          TSNode a_node = ts_node_child_by_field_name(spec_node, "alias", 5);
          std::string name = std::string(node_text(n_node, src));
          std::string alias = ts_node_is_null(a_node) ? name : std::string(node_text(a_node, src));
          imp.names.emplace_back(name, alias);
        }
        imports.push_back(std::move(imp));
      } else if (ctype == "namespace_import") {
        ImportFact imp;
        imp.kind = ImportFact::TsNamespace;
        imp.spec = spec;
        imp.line = line;
        imp.is_type_only = is_type_only;
        TSNode inner = ts_node_named_child(c, 0);
        std::string alias = std::string(node_text(inner, src));
        imp.names.emplace_back("*", alias);
        imports.push_back(std::move(imp));
      }
    }
  }
}

void extract_export_clause_or_reexport(TSNode export_node, std::vector<ImportFact>& imports, std::vector<std::string>& exports, std::string_view src) {
  uint32_t line = ts_node_start_point(export_node).row + 1;
  TSNode src_node = ts_node_child_by_field_name(export_node, "source", 6);
  std::string spec;
  bool is_reexport = false;
  if (!ts_node_is_null(src_node)) {
    spec = strip_quotes(node_text(src_node, src));
    is_reexport = true;
  }

  TSNode clause = ts_node_child_by_field_name(export_node, "export_clause", 13);
  if (ts_node_is_null(clause)) {
    uint32_t ncount = ts_node_named_child_count(export_node);
    for (uint32_t i = 0; i < ncount; ++i) {
      TSNode c = ts_node_named_child(export_node, i);
      if (std::string_view(ts_node_type(c)) == "export_clause") {
        clause = c;
        break;
      }
    }
  }

  if (!ts_node_is_null(clause)) {
    ImportFact imp;
    if (is_reexport) {
      imp.kind = ImportFact::ReExport;
      imp.spec = spec;
      imp.line = line;
    }
    uint32_t scount = ts_node_named_child_count(clause);
    for (uint32_t i = 0; i < scount; ++i) {
      TSNode spec_node = ts_node_named_child(clause, i);
      TSNode n_node = ts_node_child_by_field_name(spec_node, "name", 4);
      TSNode a_node = ts_node_child_by_field_name(spec_node, "alias", 5);
      std::string name = std::string(node_text(n_node, src));
      std::string alias = ts_node_is_null(a_node) ? name : std::string(node_text(a_node, src));
      if (is_reexport) {
        imp.names.emplace_back(name, alias);
      }
      exports.push_back(alias);
    }
    if (is_reexport && !imp.names.empty()) {
      imports.push_back(std::move(imp));
    }
  }

  if (is_reexport) {
    uint32_t count = ts_node_child_count(export_node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_child(export_node, i);
      std::string_view ctype = ts_node_type(child);
      if (ctype == "*") {
        ImportFact imp;
        imp.kind = ImportFact::ReExport;
        imp.spec = spec;
        imp.line = line;
        imp.names.emplace_back("*", "*");
        imports.push_back(std::move(imp));
      } else if (ctype == "namespace_export") {
        TSNode inner = ts_node_named_child(child, 0);
        std::string alias = std::string(node_text(inner, src));
        ImportFact imp;
        imp.kind = ImportFact::ReExport;
        imp.spec = spec;
        imp.line = line;
        imp.names.emplace_back("*", alias);
        imports.push_back(std::move(imp));
        exports.push_back(alias);
      }
    }
  }
}

void extract_commonjs(TSNode node, std::vector<ImportFact>& imports, std::vector<std::string>& exports, std::string_view src) {
  uint32_t line = ts_node_start_point(node).row + 1;
  std::string_view type = ts_node_type(node);

  // 1. const x = require('./m'); or const { a, b } = require('./m');
  if (type == "lexical_declaration" || type == "variable_declaration") {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode decl = ts_node_named_child(node, i);
      if (std::string_view(ts_node_type(decl)) != "variable_declarator") continue;
      TSNode val = ts_node_child_by_field_name(decl, "value", 5);
      if (ts_node_is_null(val) || std::string_view(ts_node_type(val)) != "call_expression") continue;

      TSNode fn = ts_node_child_by_field_name(val, "function", 8);
      if (ts_node_is_null(fn) || node_text(fn, src) != "require") continue;

      TSNode args = ts_node_child_by_field_name(val, "arguments", 9);
      if (ts_node_is_null(args) || ts_node_named_child_count(args) == 0) continue;
      TSNode first_arg = ts_node_named_child(args, 0);
      std::string spec = strip_quotes(node_text(first_arg, src));

      TSNode name_node = ts_node_child_by_field_name(decl, "name", 4);
      if (ts_node_is_null(name_node)) continue;
      std::string_view name_type = ts_node_type(name_node);

      if (name_type == "identifier") {
        ImportFact imp;
        imp.kind = ImportFact::TsNamespace;
        imp.spec = spec;
        imp.line = line;
        std::string id_name = std::string(node_text(name_node, src));
        imp.names.emplace_back("*", id_name);
        imports.push_back(std::move(imp));
      } else if (name_type == "object_pattern") {
        ImportFact imp;
        imp.kind = ImportFact::TsNamed;
        imp.spec = spec;
        imp.line = line;
        uint32_t pcount = ts_node_named_child_count(name_node);
        for (uint32_t pi = 0; pi < pcount; ++pi) {
          TSNode prop = ts_node_named_child(name_node, pi);
          std::string_view ptype = ts_node_type(prop);
          if (ptype == "shorthand_property_identifier_pattern") {
            std::string n = std::string(node_text(prop, src));
            imp.names.emplace_back(n, n);
          } else if (ptype == "pair_pattern") {
            TSNode k = ts_node_child_by_field_name(prop, "key", 3);
            TSNode v = ts_node_child_by_field_name(prop, "value", 5);
            std::string key = std::string(node_text(k, src));
            std::string val_alias = std::string(node_text(v, src));
            imp.names.emplace_back(key, val_alias);
          }
        }
        if (!imp.names.empty()) {
          imports.push_back(std::move(imp));
        }
      }
    }
  }

  // 2. Bare require('./m');
  if (type == "expression_statement") {
    TSNode expr = ts_node_named_child(node, 0);
    if (!ts_node_is_null(expr) && std::string_view(ts_node_type(expr)) == "call_expression") {
      TSNode fn = ts_node_child_by_field_name(expr, "function", 8);
      if (!ts_node_is_null(fn) && node_text(fn, src) == "require") {
        TSNode args = ts_node_child_by_field_name(expr, "arguments", 9);
        if (!ts_node_is_null(args) && ts_node_named_child_count(args) > 0) {
          TSNode first_arg = ts_node_named_child(args, 0);
          ImportFact imp;
          imp.kind = ImportFact::TsSideEffect;
          imp.spec = strip_quotes(node_text(first_arg, src));
          imp.line = line;
          imports.push_back(std::move(imp));
        }
      }
    }
    // 3. module.exports = ... or exports.foo = ...
    if (!ts_node_is_null(expr) && std::string_view(ts_node_type(expr)) == "assignment_expression") {
      TSNode left = ts_node_child_by_field_name(expr, "left", 4);
      TSNode right = ts_node_child_by_field_name(expr, "right", 5);
      std::string left_text = std::string(node_text(left, src));
      if (left_text == "module.exports") {
        if (!ts_node_is_null(right)) {
          std::string_view rtype = ts_node_type(right);
          if (rtype == "object") {
            uint32_t prop_count = ts_node_named_child_count(right);
            for (uint32_t pi = 0; pi < prop_count; ++pi) {
              TSNode prop = ts_node_named_child(right, pi);
              std::string_view pt = ts_node_type(prop);
              if (pt == "shorthand_property_identifier") {
                exports.push_back(std::string(node_text(prop, src)));
              } else if (pt == "pair") {
                TSNode k = ts_node_child_by_field_name(prop, "key", 3);
                exports.push_back(std::string(node_text(k, src)));
              }
            }
          } else if (rtype == "identifier") {
            exports.push_back(std::string(node_text(right, src)));
          }
        }
      } else if (left_text.starts_with("exports.") || left_text.starts_with("module.exports.")) {
        if (std::string_view(ts_node_type(left)) == "member_expression") {
          TSNode prop = ts_node_child_by_field_name(left, "property", 8);
          if (!ts_node_is_null(prop)) {
            exports.push_back(std::string(node_text(prop, src)));
          }
        }
      }
    }
  }
}

void resolve_duplicate_ids(std::vector<SymbolFact>& symbols, const std::string& file) {
  std::map<std::string, std::vector<size_t>> by_qualname;
  for (size_t i = 0; i < symbols.size(); ++i) {
    by_qualname[symbols[i].qualname].push_back(i);
  }
  for (auto& [qname, indices] : by_qualname) {
    if (indices.size() > 1) {
      size_t last_idx = indices.back();
      symbols[last_idx].id = file + "::" + qname;
      for (size_t k = 0; k < indices.size() - 1; ++k) {
        symbols[indices[k]].id = file + "::" + qname + "#" + std::to_string(k + 2);
      }
    }
  }
}

} // namespace gs::lang::ts

