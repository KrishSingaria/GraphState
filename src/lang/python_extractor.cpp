#include "lang/extractor.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "core/hash.hpp"
#include "lang/metrics.hpp"
#include "lang/syntax.hpp"

namespace gs::lang {

namespace {

std::string_view node_text(TSNode node, std::string_view src) {
  if (ts_node_is_null(node)) return "";
  uint32_t sb = ts_node_start_byte(node);
  uint32_t eb = ts_node_end_byte(node);
  if (sb >= src.size() || eb > src.size() || sb > eb) return "";
  return src.substr(sb, eb - sb);
}

std::string trim_str(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

std::string strip_quotes(std::string_view s) {
  s = trim_str(s);
  if (s.size() >= 6 && s.substr(0, 3) == "\"\"\"" && s.substr(s.size() - 3) == "\"\"\"") {
    s = s.substr(3, s.size() - 6);
  } else if (s.size() >= 6 && s.substr(0, 3) == "'''" && s.substr(s.size() - 3) == "'''") {
    s = s.substr(3, s.size() - 6);
  } else if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
    s = s.substr(1, s.size() - 2);
  }
  size_t nl = s.find_first_of("\r\n");
  if (nl != std::string_view::npos) {
    s = s.substr(0, nl);
  }
  std::string out = trim_str(s);
  if (out.size() > 160) {
    out = out.substr(0, 160);
  }
  return out;
}

std::string extract_decorator_name(TSNode dec_node, std::string_view src) {
  std::string_view txt = node_text(dec_node, src);
  if (txt.starts_with("@")) {
    txt.remove_prefix(1);
  }
  size_t p = txt.find('(');
  if (p != std::string_view::npos) {
    txt = txt.substr(0, p);
  }
  return trim_str(txt);
}

void collect_assigned_identifiers(TSNode node, std::string_view src, std::vector<std::string>& out) {
  if (ts_node_is_null(node)) return;
  std::string_view type = ts_node_type(node);
  if (type == "identifier") {
    out.emplace_back(node_text(node, src));
    return;
  }
  uint32_t count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_assigned_identifiers(ts_node_named_child(node, i), src, out);
  }
}

class PythonExtractorImpl : public Extractor {
public:
  FileFacts extract(const std::string& file, const std::string& src, TSTree* tree) const override {
    FileFacts facts;
    facts.file = file;
    facts.hash = core::xxh3_hex(src);
    facts.lang = Language::Python;
    facts.size = src.size();

    // Count lines
    facts.line_count = 1;
    for (char c : src) {
      if (c == '\n') facts.line_count++;
    }

    if (!tree) {
      facts.parse_ok = false;
      return facts;
    }

    TSNode root = ts_tree_root_node(tree);
    facts.parse_ok = !ts_node_has_error(root);
    if (!facts.parse_ok) {
      facts.syntax_errors = collect_syntax_errors(root, src, 50);
    }

    // Add module pseudo-symbol
    SymbolFact mod_sym;
    mod_sym.id = file + "::<module>";
    mod_sym.name = "<module>";
    mod_sym.qualname = "<module>";
    mod_sym.kind = SymbolKind::Module;
    mod_sym.def_range = {0, static_cast<uint32_t>(src.size())};
    mod_sym.body_range = mod_sym.def_range;
    mod_sym.lines = {1, facts.line_count};
    mod_sym.body_hash = core::body_hash(src);
    facts.symbols.push_back(mod_sym);

    Context ctx{file, src, facts, mod_sym.id};
    walk_module_level(root, ctx);

    // Attribute duplicate qualnames (#2, #3...)
    resolve_duplicate_ids(facts.symbols, file);

    return facts;
  }

private:
  struct Context {
    const std::string& file;
    const std::string& src;
    FileFacts& facts;
    std::string current_caller_id;
  };

  void walk_module_level(TSNode node, Context& ctx) const {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_named_child(node, i);
      std::string_view type = ts_node_type(child);

      if (type == "function_definition") {
        extract_function(child, child, {}, "", ctx);
      } else if (type == "decorated_definition") {
        extract_decorated(child, "", ctx);
      } else if (type == "class_definition") {
        extract_class(child, child, {}, "", ctx);
      } else if (type == "import_statement") {
        extract_import(child, ctx);
      } else if (type == "import_from_statement") {
        extract_import_from(child, ctx);
      } else if (type == "expression_statement") {
        TSNode expr = ts_node_named_child(child, 0);
        if (!ts_node_is_null(expr) && std::string_view(ts_node_type(expr)) == "assignment") {
          TSNode left = ts_node_child_by_field_name(expr, "left", 4);
          collect_assigned_identifiers(left, ctx.src, ctx.facts.module_names);
          TSNode right = ts_node_child_by_field_name(expr, "right", 5);
          if (!ts_node_is_null(right)) {
            collect_calls_in_node(right, ctx.file + "::<module>", ctx);
          }
        } else {
          collect_calls_in_node(child, ctx.file + "::<module>", ctx);
        }
      } else if (type == "if_statement" || type == "try_statement" || type == "with_statement") {
        // Recurse into blocks with module-level rules (e.g. if TYPE_CHECKING:)
        walk_module_level(child, ctx);
      } else {
        collect_calls_in_node(child, ctx.file + "::<module>", ctx);
      }
    }
  }

  void extract_decorated(TSNode dec_def, std::string_view parent_class, Context& ctx) const {
    std::vector<std::string> decorators;
    uint32_t child_count = ts_node_named_child_count(dec_def);
    TSNode def_node = ts_node_child_by_field_name(dec_def, "definition", 10);

    for (uint32_t i = 0; i < child_count; ++i) {
      TSNode c = ts_node_named_child(dec_def, i);
      if (std::string_view(ts_node_type(c)) == "decorator") {
        decorators.push_back(extract_decorator_name(c, ctx.src));
      }
    }

    if (!ts_node_is_null(def_node)) {
      std::string_view def_type = ts_node_type(def_node);
      if (def_type == "function_definition") {
        extract_function(def_node, dec_def, decorators, parent_class, ctx);
      } else if (def_type == "class_definition") {
        extract_class(def_node, dec_def, decorators, parent_class, ctx);
      }
    }
  }

  void extract_function(TSNode fn_node, TSNode def_range_node, const std::vector<std::string>& decorators,
                        std::string_view parent_class, Context& ctx) const {
    TSNode name_node = ts_node_child_by_field_name(fn_node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    std::string name = std::string(node_text(name_node, ctx.src));
    std::string qualname = parent_class.empty() ? name : (std::string(parent_class) + "." + name);

    SymbolFact sym;
    sym.name = name;
    sym.qualname = qualname;
    sym.id = ctx.file + "::" + qualname;
    sym.class_qualname = std::string(parent_class);
    sym.decorators = decorators;

    // SymbolKind
    if (!parent_class.empty()) {
      sym.kind = (name == "__init__") ? SymbolKind::Constructor : SymbolKind::Method;
    } else {
      sym.kind = SymbolKind::Function;
    }

    // is_async
    TSNode first_child = ts_node_child(fn_node, 0);
    sym.sig.is_async = (!ts_node_is_null(first_child) && std::string_view(ts_node_type(first_child)) == "async");

    // is_static
    sym.is_static = false;
    for (const auto& dec : decorators) {
      if (dec == "staticmethod") {
        sym.is_static = true;
        break;
      }
    }

    // drop_first_for_instance_call
    sym.sig.drop_first_for_instance_call = (sym.kind == SymbolKind::Method || sym.kind == SymbolKind::Constructor) && !sym.is_static;

    // is_exported: !starts_with("_") || (starts_with("__") && ends_with("__"))
    if (name.size() >= 4 && name.starts_with("__") && name.ends_with("__")) {
      sym.is_exported = true;
    } else if (!name.starts_with("_")) {
      sym.is_exported = true;
    } else {
      sym.is_exported = false;
    }

    // return_type
    TSNode ret_node = ts_node_child_by_field_name(fn_node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
      std::string_view rt = node_text(ret_node, ctx.src);
      if (rt.starts_with("->")) {
        rt.remove_prefix(2);
      }
      sym.sig.return_type = trim_str(rt);
    }

    // Parameters
    TSNode params_node = ts_node_child_by_field_name(fn_node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
      extract_params(params_node, sym.sig.params, ctx.src);
    }

    // def_range & lines
    sym.def_range = {ts_node_start_byte(def_range_node), ts_node_end_byte(def_range_node)};
    TSPoint sp = ts_node_start_point(def_range_node);
    TSPoint ep = ts_node_end_point(def_range_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    // body_range & docstring
    TSNode body_node = ts_node_child_by_field_name(fn_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
      sym.body_range = {ts_node_start_byte(body_node), ts_node_end_byte(body_node)};
      sym.body_hash = core::body_hash(node_text(body_node, ctx.src));

      // Docstring: first statement if expression_statement -> string
      TSNode first_stmt = ts_node_named_child(body_node, 0);
      if (!ts_node_is_null(first_stmt) && std::string_view(ts_node_type(first_stmt)) == "expression_statement") {
        TSNode inner = ts_node_named_child(first_stmt, 0);
        if (!ts_node_is_null(inner) && std::string_view(ts_node_type(inner)) == "string") {
          sym.doc = strip_quotes(node_text(inner, ctx.src));
        }
      }

      // Metrics
      auto m = compute_metrics(body_node, Language::Python, ctx.src);
      sym.complexity = m.complexity;
      sym.max_nesting = m.max_nesting;

      // Function locals
      collect_locals(fn_node, sym.locals, ctx.src);

      // Collect calls inside function
      collect_calls_in_node(body_node, sym.id, ctx);
    }

    ctx.facts.symbols.push_back(std::move(sym));
  }

  void extract_params(TSNode params_node, std::vector<Param>& out, std::string_view src) const {
    bool after_star = false;
    uint32_t count = ts_node_child_count(params_node);

    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_child(params_node, i);
      std::string_view type = ts_node_type(child);

      if (type == "(" || type == ")" || type == ",") {
        continue;
      }

      if (type == "keyword_separator" || type == "*") {
        after_star = true;
        continue;
      }

      if (type == "positional_separator" || type == "/") {
        for (auto& p : out) {
          p.kind = ParamKind::PositionalOnly;
        }
        continue;
      }

      Param p;
      p.kind = after_star ? ParamKind::KeywordOnly : ParamKind::PositionalOrKeyword;

      if (type == "identifier") {
        p.name = std::string(node_text(child, src));
      } else if (type == "typed_parameter") {
        TSNode inner = ts_node_named_child(child, 0);
        if (!ts_node_is_null(inner)) {
          std::string_view inner_type = ts_node_type(inner);
          if (inner_type == "list_splat_pattern") {
            p.kind = ParamKind::VarPositional;
            p.name = std::string(node_text(ts_node_named_child(inner, 0), src));
            after_star = true;
          } else if (inner_type == "dictionary_splat_pattern") {
            p.kind = ParamKind::VarKeyword;
            p.name = std::string(node_text(ts_node_named_child(inner, 0), src));
          } else {
            p.name = std::string(node_text(inner, src));
          }
        }
        TSNode t_node = ts_node_child_by_field_name(child, "type", 4);
        if (!ts_node_is_null(t_node)) {
          p.type = trim_str(node_text(t_node, src));
        }
      } else if (type == "default_parameter") {
        TSNode name_n = ts_node_child_by_field_name(child, "name", 4);
        p.name = std::string(node_text(name_n, src));
        p.has_default = true;
      } else if (type == "typed_default_parameter") {
        TSNode name_n = ts_node_child_by_field_name(child, "name", 4);
        p.name = std::string(node_text(name_n, src));
        p.has_default = true;
        TSNode t_node = ts_node_child_by_field_name(child, "type", 4);
        if (!ts_node_is_null(t_node)) {
          p.type = trim_str(node_text(t_node, src));
        }
      } else if (type == "list_splat_pattern") {
        p.kind = ParamKind::VarPositional;
        p.name = std::string(node_text(ts_node_named_child(child, 0), src));
        after_star = true;
      } else if (type == "dictionary_splat_pattern") {
        p.kind = ParamKind::VarKeyword;
        p.name = std::string(node_text(ts_node_named_child(child, 0), src));
      } else {
        continue;
      }

      if (!p.name.empty()) {
        out.push_back(std::move(p));
      }
    }
  }

  void extract_class(TSNode class_node, TSNode def_range_node, const std::vector<std::string>& decorators,
                     std::string_view parent_class, Context& ctx) const {
    TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    std::string name = std::string(node_text(name_node, ctx.src));
    std::string qualname = parent_class.empty() ? name : (std::string(parent_class) + "." + name);

    SymbolFact sym;
    sym.name = name;
    sym.qualname = qualname;
    sym.id = ctx.file + "::" + qualname;
    sym.kind = SymbolKind::Class;
    sym.decorators = decorators;
    sym.is_exported = !name.starts_with("_");

    // Superclasses (bases)
    TSNode sup_node = ts_node_child_by_field_name(class_node, "superclasses", 12);
    if (!ts_node_is_null(sup_node)) {
      uint32_t count = ts_node_named_child_count(sup_node);
      for (uint32_t i = 0; i < count; ++i) {
        TSNode child = ts_node_named_child(sup_node, i);
        if (std::string_view(ts_node_type(child)) != "keyword_argument") {
          sym.bases.push_back(std::string(node_text(child, ctx.src)));
        }
      }
    }

    sym.def_range = {ts_node_start_byte(def_range_node), ts_node_end_byte(def_range_node)};
    TSPoint sp = ts_node_start_point(def_range_node);
    TSPoint ep = ts_node_end_point(def_range_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    // Body
    TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
      sym.body_range = {ts_node_start_byte(body_node), ts_node_end_byte(body_node)};
      sym.body_hash = core::body_hash(node_text(body_node, ctx.src));

      // Docstring
      TSNode first_stmt = ts_node_named_child(body_node, 0);
      if (!ts_node_is_null(first_stmt) && std::string_view(ts_node_type(first_stmt)) == "expression_statement") {
        TSNode inner = ts_node_named_child(first_stmt, 0);
        if (!ts_node_is_null(inner) && std::string_view(ts_node_type(inner)) == "string") {
          sym.doc = strip_quotes(node_text(inner, ctx.src));
        }
      }

      ctx.facts.symbols.push_back(std::move(sym));

      // Class members
      uint32_t member_count = ts_node_named_child_count(body_node);
      for (uint32_t i = 0; i < member_count; ++i) {
        TSNode m = ts_node_named_child(body_node, i);
        std::string_view mtype = ts_node_type(m);
        if (mtype == "function_definition") {
          extract_function(m, m, {}, qualname, ctx);
        } else if (mtype == "decorated_definition") {
          extract_decorated(m, qualname, ctx);
        } else if (mtype == "class_definition") {
          extract_class(m, m, {}, qualname, ctx);
        } else {
          // Other statements inside class body execute at import time -> attributed to <module>
          collect_calls_in_node(m, ctx.file + "::<module>", ctx);
        }
      }
    } else {
      ctx.facts.symbols.push_back(std::move(sym));
    }
  }

  void extract_import(TSNode import_node, Context& ctx) const {
    uint32_t count = ts_node_named_child_count(import_node);
    ImportFact imp;
    imp.kind = ImportFact::Module;
    imp.line = ts_node_start_point(import_node).row + 1;

    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_named_child(import_node, i);
      std::string_view ctype = ts_node_type(child);
      if (ctype == "dotted_name") {
        std::string text = std::string(node_text(child, ctx.src));
        imp.spec = text;
        size_t dot = text.find('.');
        std::string first_seg = (dot == std::string::npos) ? text : text.substr(0, dot);
        imp.names.emplace_back(text, first_seg);
      } else if (ctype == "aliased_import") {
        TSNode name_n = ts_node_child_by_field_name(child, "name", 4);
        TSNode alias_n = ts_node_child_by_field_name(child, "alias", 5);
        std::string name = std::string(node_text(name_n, ctx.src));
        std::string alias = std::string(node_text(alias_n, ctx.src));
        imp.spec = name;
        imp.names.emplace_back(name, alias);
      }
    }
    if (!imp.spec.empty()) {
      ctx.facts.imports.push_back(std::move(imp));
    }
  }

  void extract_import_from(TSNode from_node, Context& ctx) const {
    ImportFact imp;
    imp.kind = ImportFact::FromNames;
    imp.line = ts_node_start_point(from_node).row + 1;

    TSNode mod_node = ts_node_child_by_field_name(from_node, "module_name", 11);
    if (!ts_node_is_null(mod_node)) {
      imp.spec = std::string(node_text(mod_node, ctx.src));
    }

    uint32_t count = ts_node_named_child_count(from_node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_named_child(from_node, i);
      if (child.id == mod_node.id) continue;
      std::string_view ctype = ts_node_type(child);

      if (ctype == "dotted_name") {
        std::string text = std::string(node_text(child, ctx.src));
        imp.names.emplace_back(text, text);
      } else if (ctype == "aliased_import") {
        TSNode name_n = ts_node_child_by_field_name(child, "name", 4);
        TSNode alias_n = ts_node_child_by_field_name(child, "alias", 5);
        imp.names.emplace_back(std::string(node_text(name_n, ctx.src)), std::string(node_text(alias_n, ctx.src)));
      } else if (ctype == "wildcard_import") {
        imp.names.emplace_back("*", "*");
      }
    }
    if (!imp.spec.empty() || !imp.names.empty()) {
      ctx.facts.imports.push_back(std::move(imp));
    }
  }

  void collect_calls_in_node(TSNode node, const std::string& caller_id, Context& ctx) const {
    if (ts_node_is_null(node)) return;

    std::string_view type = ts_node_type(node);
    if (type == "class_definition") {
      return; // Nested classes are separate symbols
    }

    if (type == "call") {
      extract_call(node, caller_id, ctx);
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      collect_calls_in_node(ts_node_child(node, i), caller_id, ctx);
    }
  }

  void extract_call(TSNode call_node, const std::string& caller_id, Context& ctx) const {
    CallFact cf;
    cf.caller_id = caller_id;
    TSPoint pt = ts_node_start_point(call_node);
    cf.line = pt.row + 1;
    cf.col = pt.column + 1;
    cf.start_byte = ts_node_start_byte(call_node);

    std::string_view full_text = node_text(call_node, ctx.src);
    // Single-line text <= 120 chars
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
    cf.text = trim_str(sline);

    TSNode fn_node = ts_node_child_by_field_name(call_node, "function", 8);
    if (!ts_node_is_null(fn_node)) {
      std::string_view fn_type = ts_node_type(fn_node);
      if (fn_type == "identifier") {
        cf.shape = CalleeShape::Name;
        cf.name = std::string(node_text(fn_node, ctx.src));
      } else if (fn_type == "attribute") {
        TSNode attr_n = ts_node_child_by_field_name(fn_node, "attribute", 9);
        TSNode obj_n = ts_node_child_by_field_name(fn_node, "object", 6);
        cf.name = std::string(node_text(attr_n, ctx.src));
        std::string_view obj_txt = node_text(obj_n, ctx.src);

        if (obj_txt == "self") {
          cf.shape = CalleeShape::SelfAttr;
        } else if (obj_txt == "cls") {
          cf.shape = CalleeShape::ClsAttr;
        } else if (std::string_view(ts_node_type(obj_n)) == "call") {
          TSNode sup_fn = ts_node_child_by_field_name(obj_n, "function", 8);
          if (!ts_node_is_null(sup_fn) && node_text(sup_fn, ctx.src) == "super") {
            cf.shape = CalleeShape::SuperAttr;
          } else {
            cf.shape = CalleeShape::Other;
          }
        } else {
          cf.shape = CalleeShape::Attr;
          cf.receiver = std::string(obj_txt);
        }
      } else {
        cf.shape = CalleeShape::Other;
      }
    }

    // Arguments
    TSNode args_node = ts_node_child_by_field_name(call_node, "arguments", 9);
    if (!ts_node_is_null(args_node)) {
      std::string_view atype = ts_node_type(args_node);
      if (atype == "generator_expression") {
        cf.pos_args = 1;
      } else {
        uint32_t acount = ts_node_named_child_count(args_node);
        for (uint32_t i = 0; i < acount; ++i) {
          TSNode arg = ts_node_named_child(args_node, i);
          std::string_view arg_type = ts_node_type(arg);
          if (arg_type == "keyword_argument") {
            TSNode kname = ts_node_child_by_field_name(arg, "name", 4);
            if (!ts_node_is_null(kname)) {
              cf.kw_names.push_back(std::string(node_text(kname, ctx.src)));
            }
          } else if (arg_type == "list_splat") {
            cf.has_star = true;
          } else if (arg_type == "dictionary_splat") {
            cf.has_dstar = true;
          } else {
            cf.pos_args++;
          }
        }
      }
    }

    ctx.facts.calls.push_back(std::move(cf));
  }

  void collect_locals(TSNode fn_node, std::vector<std::string>& locals, std::string_view src) const {
    // Collect parameters
    TSNode params_node = ts_node_child_by_field_name(fn_node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
      uint32_t pcount = ts_node_named_child_count(params_node);
      for (uint32_t i = 0; i < pcount; ++i) {
        collect_assigned_identifiers(ts_node_named_child(params_node, i), src, locals);
      }
    }

    TSNode body_node = ts_node_child_by_field_name(fn_node, "body", 4);
    if (!ts_node_is_null(body_node)) {
      walk_locals(body_node, locals, src);
    }
  }

  void walk_locals(TSNode node, std::vector<std::string>& locals, std::string_view src) const {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode c = ts_node_named_child(node, i);
      std::string_view type = ts_node_type(c);

      if (type == "assignment" || type == "augmented_assignment") {
        TSNode left = ts_node_child_by_field_name(c, "left", 4);
        collect_assigned_identifiers(left, src, locals);
      } else if (type == "for_statement") {
        TSNode left = ts_node_child_by_field_name(c, "left", 4);
        collect_assigned_identifiers(left, src, locals);
      } else if (type == "with_item") {
        TSNode alias = ts_node_child_by_field_name(c, "alias", 5);
        if (!ts_node_is_null(alias)) {
          collect_assigned_identifiers(alias, src, locals);
        }
      } else if (type == "except_clause") {
        TSNode as_n = ts_node_child_by_field_name(c, "name", 4);
        if (!ts_node_is_null(as_n)) {
          collect_assigned_identifiers(as_n, src, locals);
        }
      } else if (type == "function_definition" || type == "class_definition") {
        TSNode name_n = ts_node_child_by_field_name(c, "name", 4);
        if (!ts_node_is_null(name_n)) {
          locals.push_back(std::string(node_text(name_n, src)));
        }
      } else if (type == "named_expression") {
        TSNode name_n = ts_node_child_by_field_name(c, "name", 4);
        if (!ts_node_is_null(name_n)) {
          locals.push_back(std::string(node_text(name_n, src)));
        }
      } else if (type == "global_statement" || type == "nonlocal_statement") {
        uint32_t gcount = ts_node_named_child_count(c);
        for (uint32_t gi = 0; gi < gcount; ++gi) {
          locals.push_back(std::string(node_text(ts_node_named_child(c, gi), src)));
        }
      }

      walk_locals(c, locals, src);
    }
  }

  void resolve_duplicate_ids(std::vector<SymbolFact>& symbols, const std::string& file) const {
    std::map<std::string, std::vector<size_t>> by_qualname;
    for (size_t i = 0; i < symbols.size(); ++i) {
      by_qualname[symbols[i].qualname].push_back(i);
    }
    for (auto& [qname, indices] : by_qualname) {
      if (indices.size() > 1) {
        // Last one keeps plain id; earlier ones get #2, #3...
        size_t last_idx = indices.back();
        symbols[last_idx].id = file + "::" + qname;
        for (size_t k = 0; k < indices.size() - 1; ++k) {
          symbols[indices[k]].id = file + "::" + qname + "#" + std::to_string(k + 2);
        }
      }
    }
  }
};

} // namespace

std::unique_ptr<Extractor> make_python_extractor() {
  return std::make_unique<PythonExtractorImpl>();
}

} // namespace gs::lang
