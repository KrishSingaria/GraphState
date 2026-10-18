#include "lang/extractor.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "core/hash.hpp"
#include "lang/metrics.hpp"
#include "lang/syntax.hpp"
#include "lang/ts_common.hpp"

namespace gs::lang {

namespace {

class TsExtractorImpl : public Extractor {
public:
  explicit TsExtractorImpl(Language lang) : lang_(lang) {}

  FileFacts extract(const std::string& file, const std::string& src, TSTree* tree) const override {
    FileFacts facts;
    facts.file = file;
    facts.hash = core::xxh3_hex(src);
    facts.lang = lang_;
    facts.size = src.size();
    facts.parse_ok = (tree != nullptr);

    // Count lines
    uint32_t line_cnt = 0;
    if (!src.empty()) {
      line_cnt = 1;
      for (char c : src) {
        if (c == '\n') line_cnt++;
      }
    }
    facts.line_count = line_cnt;

    if (!facts.parse_ok) {
      return facts;
    }

    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) {
      return facts;
    }

    // Syntax errors
    facts.syntax_errors = collect_syntax_errors(root, src);

    // Module pseudo-symbol
    SymbolFact mod_sym;
    mod_sym.id = file + "::<module>";
    mod_sym.name = "<module>";
    mod_sym.qualname = "<module>";
    mod_sym.kind = SymbolKind::Module;
    mod_sym.def_range = {0, static_cast<uint32_t>(src.size())};
    mod_sym.body_range = {0, static_cast<uint32_t>(src.size())};
    mod_sym.lines = {1, std::max(1u, line_cnt)};
    mod_sym.loc = static_cast<int>(mod_sym.lines.end - mod_sym.lines.start + 1);
    mod_sym.is_exported = true;
    facts.symbols.push_back(std::move(mod_sym));

    std::set<std::string> pending_overloads;

    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_named_child(root, i);
      process_program_child(child, file, src, facts, pending_overloads);
    }

    // Populate module_names and exports from top-level symbols
    for (const auto& sym : facts.symbols) {
      if (sym.kind == SymbolKind::Module) continue;
      if (sym.qualname.find('.') == std::string::npos) {
        facts.module_names.push_back(sym.name);
        if (sym.is_exported) {
          facts.exports.push_back(sym.name);
        }
      }
    }

    // Sort and deduplicate module_names and exports
    std::sort(facts.module_names.begin(), facts.module_names.end());
    facts.module_names.erase(std::unique(facts.module_names.begin(), facts.module_names.end()), facts.module_names.end());

    std::sort(facts.exports.begin(), facts.exports.end());
    facts.exports.erase(std::unique(facts.exports.begin(), facts.exports.end()), facts.exports.end());

    // Resolve duplicate qualnames
    ts::resolve_duplicate_ids(facts.symbols, file);

    return facts;
  }

private:
  Language lang_;

  void process_program_child(TSNode node,
                             const std::string& file,
                             const std::string& src,
                             FileFacts& facts,
                             std::set<std::string>& pending_overloads) const {
    std::string_view type = ts_node_type(node);
    bool is_exported = false;
    TSNode target = node;

    if (type == "export_statement") {
      is_exported = true;
      TSNode decl = ts_node_child_by_field_name(node, "declaration", 11);
      if (!ts_node_is_null(decl)) {
        target = decl;
        type = ts_node_type(target);
      } else {
        // export { a, b as c } [from './m'] or export * from './m'
        ts::extract_export_clause_or_reexport(node, facts.imports, facts.exports, src);

        // Check for default export of expression: export default foo;
        TSNode val = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(val)) {
          std::string_view val_type = ts_node_type(val);
          if (val_type == "arrow_function" || val_type == "function_expression") {
            extract_arrow_or_fn_expr(val, "default", "default", node, is_exported, file, src, facts);
            return;
          }
        }

        // Collect any calls inside export statement
        std::string mod_id = file + "::<module>";
        ts::collect_calls(node, mod_id, facts.calls, src);
        return;
      }
    }

    if (type == "function_signature") {
      TSNode name_node = ts_node_child_by_field_name(target, "name", 4);
      if (!ts_node_is_null(name_node)) {
        pending_overloads.insert(std::string(ts::node_text(name_node, src)));
      }
      return;
    }

    if (type == "function_declaration" || type == "generator_function_declaration") {
      extract_function(target, node, is_exported, file, src, facts, pending_overloads);
      return;
    }

    if (type == "class_declaration" || type == "abstract_class_declaration") {
      extract_class(target, "", node, is_exported, file, src, facts);
      return;
    }

    if (type == "interface_declaration") {
      extract_interface(target, node, is_exported, file, src, facts);
      return;
    }

    if (type == "lexical_declaration" || type == "variable_declaration") {
      extract_variables(target, node, is_exported, file, src, facts);
      return;
    }

    if (type == "import_statement") {
      ts::extract_import(target, facts.imports, src);
      return;
    }

    // Anything else at program level: collect calls -> <module>
    std::string mod_id = file + "::<module>";
    ts::collect_calls(node, mod_id, facts.calls, src);
  }

  void extract_function(TSNode fn_node,
                        TSNode full_node,
                        bool is_exported,
                        const std::string& file,
                        const std::string& src,
                        FileFacts& facts,
                        std::set<std::string>& pending_overloads) const {
    TSNode name_node = ts_node_child_by_field_name(fn_node, "name", 4);
    std::string name;
    if (!ts_node_is_null(name_node)) {
      name = std::string(ts::node_text(name_node, src));
    } else {
      name = "default";
    }

    SymbolFact sym;
    sym.name = name;
    sym.qualname = name;
    sym.id = file + "::" + sym.qualname;
    sym.kind = SymbolKind::Function;
    sym.is_exported = is_exported;
    sym.doc = ts::extract_jsdoc(full_node, src);

    // Async check
    uint32_t ccount = ts_node_child_count(fn_node);
    for (uint32_t i = 0; i < ccount; ++i) {
      if (ts::node_text(ts_node_child(fn_node, i), src) == "async") {
        sym.sig.is_async = true;
        break;
      }
    }

    // Overload check
    if (pending_overloads.count(name)) {
      sym.sig.has_overloads = true;
    }

    // Return type
    TSNode ret_node = ts_node_child_by_field_name(fn_node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
      std::string_view rtxt = ts::node_text(ret_node, src);
      if (rtxt.starts_with(":")) {
        rtxt.remove_prefix(1);
      }
      sym.sig.return_type = std::string(ts::trim_str(rtxt));
    }

    // Parameters
    TSNode params_node = ts_node_child_by_field_name(fn_node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
      ts::parse_parameters(params_node, sym.sig.params, src, true);
    }

    sym.def_range = {ts_node_start_byte(full_node), ts_node_end_byte(fn_node)};
    TSPoint sp = ts_node_start_point(full_node);
    TSPoint ep = ts_node_end_point(fn_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    // Body
    TSNode body = ts_node_child_by_field_name(fn_node, "body", 4);
    if (!ts_node_is_null(body)) {
      sym.body_range = {ts_node_start_byte(body), ts_node_end_byte(body)};
      sym.body_hash = core::body_hash(ts::node_text(body, src));
      auto metrics = compute_metrics(body, lang_, src);
      sym.complexity = metrics.complexity;
      sym.max_nesting = metrics.max_nesting;
      ts::collect_calls(body, sym.id, facts.calls, src);
    }

    facts.symbols.push_back(std::move(sym));
  }

  void extract_arrow_or_fn_expr(TSNode fn_node,
                                const std::string& name,
                                const std::string& qualname,
                                TSNode full_node,
                                bool is_exported,
                                const std::string& file,
                                const std::string& src,
                                FileFacts& facts) const {
    SymbolFact sym;
    sym.name = name;
    sym.qualname = qualname;
    sym.id = file + "::" + sym.qualname;
    sym.kind = SymbolKind::Arrow;
    sym.is_exported = is_exported;
    sym.doc = ts::extract_jsdoc(full_node, src);

    // Check async
    uint32_t ccount = ts_node_child_count(fn_node);
    for (uint32_t i = 0; i < ccount; ++i) {
      if (ts::node_text(ts_node_child(fn_node, i), src) == "async") {
        sym.sig.is_async = true;
        break;
      }
    }

    // Return type
    TSNode ret_node = ts_node_child_by_field_name(fn_node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
      std::string_view rtxt = ts::node_text(ret_node, src);
      if (rtxt.starts_with(":")) {
        rtxt.remove_prefix(1);
      }
      sym.sig.return_type = std::string(ts::trim_str(rtxt));
    }

    // Parameters (either 'parameter' or 'parameters')
    TSNode params_node = ts_node_child_by_field_name(fn_node, "parameters", 10);
    if (ts_node_is_null(params_node)) {
      params_node = ts_node_child_by_field_name(fn_node, "parameter", 9);
    }
    if (!ts_node_is_null(params_node)) {
      ts::parse_parameters(params_node, sym.sig.params, src, true);
    }

    sym.def_range = {ts_node_start_byte(full_node), ts_node_end_byte(full_node)};
    TSPoint sp = ts_node_start_point(full_node);
    TSPoint ep = ts_node_end_point(full_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    // Body
    TSNode body = ts_node_child_by_field_name(fn_node, "body", 4);
    if (!ts_node_is_null(body)) {
      sym.body_range = {ts_node_start_byte(body), ts_node_end_byte(body)};
      sym.body_hash = core::body_hash(ts::node_text(body, src));
      auto metrics = compute_metrics(body, lang_, src);
      sym.complexity = metrics.complexity;
      sym.max_nesting = metrics.max_nesting;
      ts::collect_calls(body, sym.id, facts.calls, src);
    }

    facts.symbols.push_back(std::move(sym));
  }

  void extract_class(TSNode class_node,
                     const std::string& parent_qualname,
                     TSNode full_node,
                     bool is_exported,
                     const std::string& file,
                     const std::string& src,
                     FileFacts& facts) const {
    TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
    std::string name;
    if (!ts_node_is_null(name_node)) {
      name = std::string(ts::node_text(name_node, src));
    } else {
      name = "default";
    }

    std::string qualname = parent_qualname.empty() ? name : parent_qualname + "." + name;

    SymbolFact sym;
    sym.name = name;
    sym.qualname = qualname;
    sym.id = file + "::" + qualname;
    sym.kind = SymbolKind::Class;
    sym.is_exported = is_exported;
    sym.doc = ts::extract_jsdoc(full_node, src);

    // Class heritage: extends
    TSNode heritage = ts_node_child_by_field_name(class_node, "heritage", 8);
    if (ts_node_is_null(heritage)) {
      // Find named child class_heritage
      uint32_t ncount = ts_node_named_child_count(class_node);
      for (uint32_t i = 0; i < ncount; ++i) {
        TSNode c = ts_node_named_child(class_node, i);
        if (std::string_view(ts_node_type(c)) == "class_heritage") {
          heritage = c;
          break;
        }
      }
    }

    if (!ts_node_is_null(heritage)) {
      uint32_t hcount = ts_node_named_child_count(heritage);
      for (uint32_t i = 0; i < hcount; ++i) {
        TSNode hchild = ts_node_named_child(heritage, i);
        if (std::string_view(ts_node_type(hchild)) == "extends_clause") {
          TSNode val = ts_node_child_by_field_name(hchild, "value", 5);
          if (!ts_node_is_null(val)) {
            sym.bases.push_back(std::string(ts::node_text(val, src)));
          } else {
            uint32_t ecount = ts_node_named_child_count(hchild);
            for (uint32_t ei = 0; ei < ecount; ++ei) {
              sym.bases.push_back(std::string(ts::node_text(ts_node_named_child(hchild, ei), src)));
            }
          }
        }
      }
    }

    sym.def_range = {ts_node_start_byte(full_node), ts_node_end_byte(class_node)};
    TSPoint sp = ts_node_start_point(full_node);
    TSPoint ep = ts_node_end_point(class_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    TSNode body = ts_node_child_by_field_name(class_node, "body", 4);
    if (!ts_node_is_null(body)) {
      sym.body_range = {ts_node_start_byte(body), ts_node_end_byte(body)};
      sym.body_hash = core::body_hash(ts::node_text(body, src));
    }

    facts.symbols.push_back(std::move(sym));

    // Process class body members
    if (!ts_node_is_null(body)) {
      std::set<std::string> method_overloads;
      uint32_t bcount = ts_node_named_child_count(body);
      for (uint32_t i = 0; i < bcount; ++i) {
        TSNode mnode = ts_node_named_child(body, i);
        std::string_view mtype = ts_node_type(mnode);

        if (mtype == "method_signature" || mtype == "abstract_method_signature") {
          TSNode mname = ts_node_child_by_field_name(mnode, "name", 4);
          if (!ts_node_is_null(mname)) {
            method_overloads.insert(std::string(ts::node_text(mname, src)));
          }
        } else if (mtype == "method_definition") {
          extract_method(mnode, qualname, file, src, facts, method_overloads);
        } else if (mtype == "public_field_definition") {
          TSNode val = ts_node_child_by_field_name(mnode, "value", 5);
          if (!ts_node_is_null(val)) {
            std::string_view vtype = ts_node_type(val);
            if (vtype == "arrow_function" || vtype == "function_expression") {
              TSNode fname = ts_node_child_by_field_name(mnode, "name", 4);
              std::string f_id = std::string(ts::node_text(fname, src));
              extract_arrow_or_fn_expr(val, f_id, qualname + "." + f_id, mnode, false, file, src, facts);
            }
          }
        } else if (mtype == "class_declaration" || mtype == "abstract_class_declaration") {
          // Nested class
          extract_class(mnode, qualname, mnode, false, file, src, facts);
        } else {
          // Other statements in class body: calls belong to <module>
          std::string mod_id = file + "::<module>";
          ts::collect_calls(mnode, mod_id, facts.calls, src);
        }
      }
    }
  }

  void extract_method(TSNode method_node,
                      const std::string& class_qualname,
                      const std::string& file,
                      const std::string& src,
                      FileFacts& facts,
                      std::set<std::string>& method_overloads) const {
    TSNode name_node = ts_node_child_by_field_name(method_node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    std::string mname = std::string(ts::node_text(name_node, src));
    std::string mqual = class_qualname + "." + mname;

    SymbolFact sym;
    sym.name = mname;
    sym.qualname = mqual;
    sym.class_qualname = class_qualname;
    sym.id = file + "::" + mqual;
    sym.doc = ts::extract_jsdoc(method_node, src);

    if (mname == "constructor") {
      sym.kind = SymbolKind::Constructor;
    } else {
      sym.kind = SymbolKind::Method;
    }

    // Check static and async
    uint32_t ccount = ts_node_child_count(method_node);
    for (uint32_t i = 0; i < ccount; ++i) {
      std::string_view txt = ts::node_text(ts_node_child(method_node, i), src);
      if (txt == "static") sym.is_static = true;
      if (txt == "async") sym.sig.is_async = true;
    }

    if (method_overloads.count(mname)) {
      sym.sig.has_overloads = true;
    }

    // Return type
    TSNode ret_node = ts_node_child_by_field_name(method_node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
      std::string_view rtxt = ts::node_text(ret_node, src);
      if (rtxt.starts_with(":")) {
        rtxt.remove_prefix(1);
      }
      sym.sig.return_type = std::string(ts::trim_str(rtxt));
    }

    // Parameters
    TSNode params_node = ts_node_child_by_field_name(method_node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
      ts::parse_parameters(params_node, sym.sig.params, src, true);
    }

    sym.def_range = {ts_node_start_byte(method_node), ts_node_end_byte(method_node)};
    TSPoint sp = ts_node_start_point(method_node);
    TSPoint ep = ts_node_end_point(method_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    // Body
    TSNode body = ts_node_child_by_field_name(method_node, "body", 4);
    if (!ts_node_is_null(body)) {
      sym.body_range = {ts_node_start_byte(body), ts_node_end_byte(body)};
      sym.body_hash = core::body_hash(ts::node_text(body, src));
      auto metrics = compute_metrics(body, lang_, src);
      sym.complexity = metrics.complexity;
      sym.max_nesting = metrics.max_nesting;
      ts::collect_calls(body, sym.id, facts.calls, src);
    }

    facts.symbols.push_back(std::move(sym));
  }

  void extract_interface(TSNode iface_node,
                         TSNode full_node,
                         bool is_exported,
                         const std::string& file,
                         const std::string& src,
                         FileFacts& facts) const {
    TSNode name_node = ts_node_child_by_field_name(iface_node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    std::string name = std::string(ts::node_text(name_node, src));
    SymbolFact sym;
    sym.name = name;
    sym.qualname = name;
    sym.id = file + "::" + name;
    sym.kind = SymbolKind::Interface;
    sym.is_exported = is_exported;
    sym.doc = ts::extract_jsdoc(full_node, src);

    sym.def_range = {ts_node_start_byte(full_node), ts_node_end_byte(iface_node)};
    TSPoint sp = ts_node_start_point(full_node);
    TSPoint ep = ts_node_end_point(iface_node);
    sym.lines = {sp.row + 1, ep.row + 1};
    sym.indent_col = sp.column;
    sym.loc = static_cast<int>(sym.lines.end - sym.lines.start + 1);

    TSNode body = ts_node_child_by_field_name(iface_node, "body", 4);
    if (!ts_node_is_null(body)) {
      sym.body_range = {ts_node_start_byte(body), ts_node_end_byte(body)};
      sym.body_hash = core::body_hash(ts::node_text(body, src));
    }

    facts.symbols.push_back(std::move(sym));
  }

  void extract_variables(TSNode var_node,
                         TSNode full_node,
                         bool is_exported,
                         const std::string& file,
                         const std::string& src,
                         FileFacts& facts) const {
    std::string mod_id = file + "::<module>";
    uint32_t count = ts_node_named_child_count(var_node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode decl = ts_node_named_child(var_node, i);
      if (std::string_view(ts_node_type(decl)) != "variable_declarator") continue;

      TSNode name_node = ts_node_child_by_field_name(decl, "name", 4);
      TSNode val_node = ts_node_child_by_field_name(decl, "value", 5);
      if (ts_node_is_null(name_node)) continue;

      std::string_view name_type = ts_node_type(name_node);
      std::string name;
      if (name_type == "identifier") {
        name = std::string(ts::node_text(name_node, src));
      }

      if (!ts_node_is_null(val_node)) {
        std::string_view vtype = ts_node_type(val_node);
        if ((vtype == "arrow_function" || vtype == "function_expression") && !name.empty()) {
          extract_arrow_or_fn_expr(val_node, name, name, full_node, is_exported, file, src, facts);
          continue;
        } else {
          // Scan value for calls -> <module>
          ts::collect_calls(val_node, mod_id, facts.calls, src);
        }
      }

      if (!name.empty()) {
        facts.module_names.push_back(name);
        if (is_exported) {
          facts.exports.push_back(name);
        }
      }
    }
  }
};

} // namespace

std::unique_ptr<Extractor> make_ts_extractor(Language lang) {
  return std::make_unique<TsExtractorImpl>(lang);
}

} // namespace gs::lang
