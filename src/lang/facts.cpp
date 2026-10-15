#include "lang/facts.hpp"

namespace gs::lang {

std::string_view param_kind_name(ParamKind k) {
  switch (k) {
    case ParamKind::PositionalOnly: return "positional_only";
    case ParamKind::PositionalOrKeyword: return "positional_or_keyword";
    case ParamKind::VarPositional: return "var_positional";
    case ParamKind::KeywordOnly: return "keyword_only";
    case ParamKind::VarKeyword: return "var_keyword";
  }
  return "unknown";
}

std::string_view symbol_kind_name(SymbolKind k) {
  switch (k) {
    case SymbolKind::Function: return "function";
    case SymbolKind::Method: return "method";
    case SymbolKind::Constructor: return "constructor";
    case SymbolKind::Arrow: return "arrow";
    case SymbolKind::Module: return "module";
    case SymbolKind::Class: return "class";
    case SymbolKind::Interface: return "interface";
  }
  return "unknown";
}

std::string_view callee_shape_name(CalleeShape s) {
  switch (s) {
    case CalleeShape::Name: return "name";
    case CalleeShape::Attr: return "attr";
    case CalleeShape::SelfAttr: return "self_attr";
    case CalleeShape::ClsAttr: return "cls_attr";
    case CalleeShape::SuperAttr: return "super_attr";
    case CalleeShape::New: return "new";
    case CalleeShape::Other: return "other";
  }
  return "unknown";
}

std::string_view import_kind_name(ImportFact::Kind k) {
  switch (k) {
    case ImportFact::Module: return "module";
    case ImportFact::FromNames: return "from_names";
    case ImportFact::TsNamed: return "ts_named";
    case ImportFact::TsDefault: return "ts_default";
    case ImportFact::TsNamespace: return "ts_namespace";
    case ImportFact::TsSideEffect: return "ts_side_effect";
    case ImportFact::ReExport: return "re_export";
  }
  return "unknown";
}

std::string Signature::canonical(std::string_view name) const {
  std::string out;
  if (is_async) {
    out += "async ";
  }
  if (!name.empty()) {
    out += name;
  }
  out += "(";

  bool has_var_pos = false;
  for (const auto& p : params) {
    if (p.kind == ParamKind::VarPositional) {
      has_var_pos = true;
      break;
    }
  }

  bool in_kw_only = false;
  size_t last_pos_only_idx = static_cast<size_t>(-1);
  for (size_t i = 0; i < params.size(); ++i) {
    if (params[i].kind == ParamKind::PositionalOnly) {
      last_pos_only_idx = i;
    }
  }

  bool first = true;
  for (size_t i = 0; i < params.size(); ++i) {
    const auto& p = params[i];

    // Check if we need to insert bare '*' separator before keyword-only
    if (!has_var_pos && !in_kw_only && p.kind == ParamKind::KeywordOnly) {
      if (!first) out += ", ";
      out += "*";
      first = false;
      in_kw_only = true;
    }

    if (!first) {
      out += ", ";
    }
    first = false;

    if (p.kind == ParamKind::VarPositional) {
      out += "*";
    } else if (p.kind == ParamKind::VarKeyword) {
      out += "**";
    }

    out += p.name;
    if (p.optional) {
      out += "?";
    }
    if (!p.type.empty()) {
      out += ": ";
      out += p.type;
    }
    if (p.has_default) {
      out += " = ...";
    }

    // Check if we need to insert '/' separator after positional-only
    if (i == last_pos_only_idx) {
      out += ", /";
    }
  }

  out += ")";
  if (!return_type.empty()) {
    out += " -> ";
    out += return_type;
  }
  return out;
}

nlohmann::json Signature::to_json() const {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& p : params) {
    nlohmann::json item = {
        {"name", p.name},
        {"kind", std::string(param_kind_name(p.kind))},
        {"has_default", p.has_default},
        {"optional", p.optional},
        {"type", p.type}
    };
    arr.push_back(item);
  }
  return arr;
}

int Signature::required_count() const {
  size_t start = (drop_first_for_instance_call && !params.empty()) ? 1 : 0;
  int count = 0;
  for (size_t i = start; i < params.size(); ++i) {
    const auto& p = params[i];
    if (p.kind != ParamKind::VarPositional && p.kind != ParamKind::VarKeyword) {
      if (!p.has_default && !p.optional) {
        count++;
      }
    }
  }
  return count;
}

int Signature::max_count() const {
  for (const auto& p : params) {
    if (p.kind == ParamKind::VarPositional) {
      return -1;
    }
  }
  int pos_count = 0;
  size_t start = (drop_first_for_instance_call && !params.empty()) ? 1 : 0;
  for (size_t i = start; i < params.size(); ++i) {
    const auto& p = params[i];
    if (p.kind == ParamKind::PositionalOnly || p.kind == ParamKind::PositionalOrKeyword) {
      pos_count++;
    }
  }
  return pos_count;
}

const SymbolFact* FileFacts::find(std::string_view qualname_or_id) const {
  for (const auto& sym : symbols) {
    if (sym.id == qualname_or_id || sym.qualname == qualname_or_id) {
      return &sym;
    }
  }
  return nullptr;
}

nlohmann::json FileFacts::to_json() const {
  nlohmann::json j;
  j["file"] = file;
  j["hash"] = hash;
  j["lang"] = std::string(LanguageRegistry::get().info(lang).name);
  j["size"] = size;
  j["mtime_ns"] = mtime_ns;
  j["line_count"] = line_count;
  j["parse_ok"] = parse_ok;
  j["skipped_reason"] = skipped_reason;

  // Syntax errors
  nlohmann::json errs = nlohmann::json::array();
  for (const auto& err : syntax_errors) {
    errs.push_back({
        {"line", err.line},
        {"col", err.col},
        {"start_byte", err.start_byte},
        {"missing", err.missing},
        {"expected", err.expected}
    });
  }
  j["syntax_errors"] = errs;

  // Symbols
  nlohmann::json syms = nlohmann::json::array();
  for (const auto& s : symbols) {
    nlohmann::json sj;
    sj["id"] = s.id;
    sj["name"] = s.name;
    sj["qualname"] = s.qualname;
    sj["kind"] = std::string(symbol_kind_name(s.kind));
    if (!s.class_qualname.empty()) sj["class_qualname"] = s.class_qualname;
    sj["is_exported"] = s.is_exported;
    sj["is_static"] = s.is_static;
    sj["def_range"] = {{"start", s.def_range.start}, {"end", s.def_range.end}};
    sj["body_range"] = {{"start", s.body_range.start}, {"end", s.body_range.end}};
    sj["lines"] = {{"start", s.lines.start}, {"end", s.lines.end}};
    sj["indent_col"] = s.indent_col;
    sj["body_hash"] = s.body_hash;
    sj["doc"] = s.doc;
    sj["complexity"] = s.complexity;
    sj["max_nesting"] = s.max_nesting;
    sj["loc"] = s.loc;
    if (!s.bases.empty()) sj["bases"] = s.bases;
    if (!s.decorators.empty()) sj["decorators"] = s.decorators;
    if (!s.locals.empty()) sj["locals"] = s.locals;

    if (s.kind != SymbolKind::Class && s.kind != SymbolKind::Interface) {
      sj["signature"] = s.sig.canonical(s.name);
      sj["params"] = s.sig.to_json();
      sj["required_params"] = s.sig.required_count();
      sj["max_params"] = s.sig.max_count();
      sj["is_async"] = s.sig.is_async;
      sj["return_type"] = s.sig.return_type;
    }
    syms.push_back(sj);
  }
  j["symbols"] = syms;

  // Calls
  nlohmann::json cls = nlohmann::json::array();
  for (const auto& c : calls) {
    cls.push_back({
        {"caller_id", c.caller_id},
        {"shape", std::string(callee_shape_name(c.shape))},
        {"name", c.name},
        {"receiver", c.receiver},
        {"line", c.line},
        {"col", c.col},
        {"start_byte", c.start_byte},
        {"pos_args", c.pos_args},
        {"kw_names", c.kw_names},
        {"has_star", c.has_star},
        {"has_dstar", c.has_dstar},
        {"text", c.text}
    });
  }
  j["calls"] = cls;

  // Imports
  nlohmann::json imps = nlohmann::json::array();
  for (const auto& imp : imports) {
    nlohmann::json names_arr = nlohmann::json::array();
    for (const auto& p : imp.names) {
      names_arr.push_back({{"imported", p.first}, {"alias", p.second}});
    }
    imps.push_back({
        {"kind", std::string(import_kind_name(imp.kind))},
        {"spec", imp.spec},
        {"names", names_arr},
        {"line", imp.line},
        {"is_type_only", imp.is_type_only}
    });
  }
  j["imports"] = imps;

  j["module_names"] = module_names;
  j["exports"] = exports;

  return j;
}

} // namespace gs::lang
