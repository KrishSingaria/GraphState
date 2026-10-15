#include "index/graph_writer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "db/queries.hpp"
#include "lang/language.hpp"

namespace gs::index {

namespace {

std::string build_search_text(const lang::SymbolFact& sym) {
  std::string res;
  std::string_view name = sym.name;
  std::string current;
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name[i];
    if (c == '_') {
      if (!current.empty()) {
        if (!res.empty()) res += " ";
        res += current;
        current.clear();
      }
    } else if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
               std::islower(static_cast<unsigned char>(name[i - 1]))) {
      if (!current.empty()) {
        if (!res.empty()) res += " ";
        res += current;
        current.clear();
      }
      current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  if (!current.empty()) {
    if (!res.empty()) res += " ";
    res += current;
  }

  std::string raw_lower;
  raw_lower.reserve(name.size());
  for (char c : name) {
    raw_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (raw_lower != res) {
    if (!res.empty()) res += " ";
    res += raw_lower;
  }
  if (!sym.doc.empty()) {
    if (!res.empty()) res += " ";
    res += sym.doc;
  }
  return res;
}

} // namespace

GraphWriter::GraphWriter(db::GraphClient& client, std::string graph)
    : client_(client), graph_(std::move(graph)) {}

void GraphWriter::sync_files(
    const std::vector<const lang::FileFacts*>& files,
    const std::vector<ResolvedImport>& resolved_imports,
    const std::vector<ResolvedInherit>& resolved_inherits,
    int64_t now) {
  if (files.empty()) return;

  const size_t BATCH_SIZE = 50;
  for (size_t start_idx = 0; start_idx < files.size(); start_idx += BATCH_SIZE) {
    size_t end_idx = std::min(start_idx + BATCH_SIZE, files.size());

    nlohmann::json rows_files = nlohmann::json::array();
    nlohmann::json keep_funcs = nlohmann::json::array();
    nlohmann::json keep_classes = nlohmann::json::array();
    nlohmann::json rows_classes = nlohmann::json::array();
    nlohmann::json rows_funcs = nlohmann::json::array();
    nlohmann::json rows_defines = nlohmann::json::array();
    nlohmann::json rows_methods = nlohmann::json::array();
    nlohmann::json rows_inherits = nlohmann::json::array();
    nlohmann::json rows_imports = nlohmann::json::array();
    std::vector<std::string> files_list;
    std::unordered_set<std::string> chunk_files;

    for (size_t i = start_idx; i < end_idx; ++i) {
      const auto* facts = files[i];
      if (!facts) continue;

      files_list.push_back(facts->file);
      chunk_files.insert(facts->file);

      int symbol_count = 0;
      for (const auto& sym : facts->symbols) {
        if (sym.kind != lang::SymbolKind::Module) {
          ++symbol_count;
        }
      }

      bool semantic = lang::LanguageRegistry::get().info(facts->lang).semantic;

      rows_files.push_back({
        {"id", facts->file},
        {"language", std::string(lang::LanguageRegistry::get().info(facts->lang).name)},
        {"semantic", semantic},
        {"hash", facts->hash},
        {"size", facts->size},
        {"mtime_ns", facts->mtime_ns},
        {"line_count", facts->line_count},
        {"parse_ok", facts->parse_ok},
        {"symbol_count", symbol_count},
        {"skipped_reason", facts->skipped_reason}
      });

      for (const auto& sym : facts->symbols) {
        if (sym.kind == lang::SymbolKind::Class || sym.kind == lang::SymbolKind::Interface) {
          keep_classes.push_back(sym.id);
          rows_classes.push_back({
            {"id", sym.id},
            {"name", sym.name},
            {"qualname", sym.qualname},
            {"file", facts->file},
            {"kind", std::string(lang::symbol_kind_name(sym.kind))},
            {"bases", sym.bases},
            {"is_exported", sym.is_exported},
            {"start_line", sym.lines.start},
            {"end_line", sym.lines.end},
            {"start_byte", sym.def_range.start},
            {"end_byte", sym.def_range.end},
            {"doc", sym.doc},
            {"search_text", build_search_text(sym)}
          });
        } else {
          keep_funcs.push_back(sym.id);
          std::string class_id = sym.class_qualname.empty() ? "" : (facts->file + "::" + sym.class_qualname);
          rows_funcs.push_back({
            {"id", sym.id},
            {"name", sym.name},
            {"qualname", sym.qualname},
            {"file", facts->file},
            {"kind", std::string(lang::symbol_kind_name(sym.kind))},
            {"class_id", class_id},
            {"signature", sym.sig.canonical(sym.name)},
            {"params_json", sym.sig.to_json().dump()},
            {"required_params", sym.sig.required_count()},
            {"max_params", sym.sig.max_count()},
            {"is_async", sym.sig.is_async},
            {"is_exported", sym.is_exported},
            {"is_static", sym.is_static},
            {"decorators", sym.decorators},
            {"return_type", sym.sig.return_type},
            {"start_line", sym.lines.start},
            {"end_line", sym.lines.end},
            {"start_byte", sym.def_range.start},
            {"end_byte", sym.def_range.end},
            {"body_hash", sym.body_hash},
            {"loc", sym.loc},
            {"complexity", sym.complexity},
            {"max_nesting", sym.max_nesting},
            {"doc", sym.doc},
            {"search_text", build_search_text(sym)}
          });
        }

        if (sym.class_qualname.empty()) {
          rows_defines.push_back({
            {"file", facts->file},
            {"sym", sym.id}
          });
        } else if (sym.kind == lang::SymbolKind::Method || sym.kind == lang::SymbolKind::Constructor) {
          rows_methods.push_back({
            {"cls", facts->file + "::" + sym.class_qualname},
            {"fn", sym.id}
          });
        }
      }
    }

    for (const auto& inh : resolved_inherits) {
      size_t sep = inh.cls.find("::");
      if (sep != std::string::npos && chunk_files.contains(inh.cls.substr(0, sep))) {
        rows_inherits.push_back({
          {"cls", inh.cls},
          {"base", inh.base},
          {"confidence", inh.confidence}
        });
      }
    }

    for (const auto& imp : resolved_imports) {
      if (chunk_files.contains(imp.src_file) && !imp.target_file.empty()) {
        rows_imports.push_back({
          {"src", imp.src_file},
          {"dst", imp.target_file},
          {"names", imp.names},
          {"line", imp.line},
          {"whole_module", imp.whole_module},
          {"alias", imp.alias}
        });
      }
    }

    std::vector<db::Query> qs;
    qs.push_back({"files_upsert", db::Q_FILES_UPSERT, {{"rows", rows_files}, {"now", now}}, false});
    qs.push_back({"functions_delete_stale", db::Q_FUNCTIONS_DELETE_STALE, {{"files", files_list}, {"keep", keep_funcs}}, false});
    qs.push_back({"classes_delete_stale", db::Q_CLASSES_DELETE_STALE, {{"files", files_list}, {"keep", keep_classes}}, false});

    if (!rows_classes.empty()) {
      qs.push_back({"classes_upsert", db::Q_CLASSES_UPSERT, {{"rows", rows_classes}}, false});
    }
    if (!rows_funcs.empty()) {
      qs.push_back({"functions_upsert", db::Q_FUNCTIONS_UPSERT, {{"rows", rows_funcs}}, false});
    }

    qs.push_back({"struct_edges_clear", db::Q_STRUCT_EDGES_CLEAR, {{"files", files_list}}, false});
    qs.push_back({"class_edges_clear", db::Q_CLASS_EDGES_CLEAR, {{"files", files_list}}, false});

    if (!rows_defines.empty()) {
      qs.push_back({"defines_create", db::Q_DEFINES_CREATE, {{"rows", rows_defines}}, false});
    }
    if (!rows_methods.empty()) {
      qs.push_back({"has_method_create", db::Q_HAS_METHOD_CREATE, {{"rows", rows_methods}}, false});
    }
    if (!rows_inherits.empty()) {
      qs.push_back({"inherits_create", db::Q_INHERITS_CREATE, {{"rows", rows_inherits}}, false});
    }
    if (!rows_imports.empty()) {
      qs.push_back({"imports_create", db::Q_IMPORTS_CREATE, {{"rows", rows_imports}}, false});
    }

    client_.transaction(graph_, qs);
  }
}

void GraphWriter::sync_files(
    const std::vector<const lang::FileFacts*>& files,
    const std::vector<ResolvedImport>& resolved_imports,
    int64_t now) {
  sync_files(files, resolved_imports, {}, now);
}

void GraphWriter::sync_files(
    const std::vector<const lang::FileFacts*>& files,
    int64_t now) {
  sync_files(files, {}, {}, now);
}

void GraphWriter::delete_files(const std::vector<std::string>& files) {
  if (files.empty()) return;

  std::vector<db::Query> qs;
  qs.push_back({"functions_delete_stale", db::Q_FUNCTIONS_DELETE_STALE, {{"files", files}, {"keep", nlohmann::json::array()}}, false});
  qs.push_back({"classes_delete_stale", db::Q_CLASSES_DELETE_STALE, {{"files", files}, {"keep", nlohmann::json::array()}}, false});
  qs.push_back({"struct_edges_clear", db::Q_STRUCT_EDGES_CLEAR, {{"files", files}}, false});
  qs.push_back({"class_edges_clear", db::Q_CLASS_EDGES_CLEAR, {{"files", files}}, false});
  qs.push_back({"calls_clear_from_files", db::Q_CALLS_CLEAR_FROM_FILES, {{"files", files}}, false});
  qs.push_back({"files_delete", db::Q_FILES_DELETE, {{"files", files}}, false});

  client_.transaction(graph_, qs);
}

void GraphWriter::replace_calls(
    const std::vector<std::string>& affected_files,
    const std::vector<ResolvedCall>& calls) {
  if (affected_files.empty()) return;

  client_.exec(graph_, {"calls_clear_from_files", db::Q_CALLS_CLEAR_FROM_FILES, {{"files", affected_files}}, false});

  const size_t CHUNK_SIZE = 2000;
  for (size_t start_idx = 0; start_idx < calls.size(); start_idx += CHUNK_SIZE) {
    size_t end_idx = std::min(start_idx + CHUNK_SIZE, calls.size());

    nlohmann::json rows = nlohmann::json::array();
    for (size_t i = start_idx; i < end_idx; ++i) {
      const auto& c = calls[i];
      if (c.caller_id.empty() || c.target_id.empty()) continue;

      rows.push_back({
        {"caller", c.caller_id},
        {"callee", c.target_id},
        {"line", c.line},
        {"col", c.col},
        {"pos_args", c.pos_args},
        {"kw_names", c.kw_names},
        {"has_star", c.has_star},
        {"has_dstar", c.has_dstar},
        {"confidence", c.confidence},
        {"call_kind", c.call_kind},
        {"call_text", c.call_text}
      });
    }

    if (!rows.empty()) {
      client_.exec(graph_, {"calls_create", db::Q_CALLS_CREATE, {{"rows", rows}}, false});
    }
  }
}

void GraphWriter::wipe_code_graph() {
  client_.exec(graph_, {"code_wipe", db::Q_CODE_WIPE, nlohmann::json::object(), false});
}

void GraphWriter::touch_indexed(int64_t now) {
  client_.exec(graph_, {"meta_touch_indexed", db::Q_META_TOUCH_INDEXED, {{"now", now}}, false});
}

void GraphWriter::relink_memory(const std::vector<std::string>& symbol_ids) {
  if (symbol_ids.empty()) return;

  std::vector<db::Query> qs;
  qs.push_back({"relink_pitfalls", db::Q_RELINK_PITFALLS, {{"ids", symbol_ids}}, false});
  qs.push_back({"relink_memories", db::Q_RELINK_MEMORIES, {{"ids", symbol_ids}}, false});
  client_.transaction(graph_, qs);
}

} // namespace gs::index
