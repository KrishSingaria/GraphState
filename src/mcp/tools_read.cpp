#include "mcp/tool_registry.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>
#include "core/clock.hpp"
#include "core/engine.hpp"
#include "core/hash.hpp"
#include "core/paths.hpp"
#include "core/text.hpp"
#include "db/queries.hpp"
#include "lang/language.hpp"
#include "platform/platform.hpp"

namespace gs::mcp {

namespace {

struct ResolvedSymbol {
  const lang::SymbolFact* sym = nullptr;
  std::string file;
};

ResolvedSymbol resolve_symbol_ref(
    const index::FactStore& fact_store,
    std::string_view ref,
    std::string_view file_filter) {
  // 1. Exact ID match
  if (const auto* s = fact_store.symbol(ref)) {
    if (file_filter.empty() || s->id.starts_with(std::string(file_filter) + "::")) {
      std::string f = s->id.substr(0, s->id.find("::"));
      return {s, std::move(f)};
    }
  }

  // 2. If ref has "::", split into file and qualname/name
  size_t colons = ref.find("::");
  std::string target_file = std::string(file_filter);
  std::string_view target_name = ref;
  if (colons != std::string_view::npos) {
    target_file = std::string(ref.substr(0, colons));
    target_name = ref.substr(colons + 2);
  }

  // 3. Search by qualname
  auto qual_matches = fact_store.find_symbols_by_qualname(target_name);
  std::vector<const lang::SymbolFact*> filtered;
  for (const auto* s : qual_matches) {
    if (target_file.empty() || s->id.starts_with(target_file + "::")) {
      filtered.push_back(s);
    }
  }

  if (filtered.size() == 1) {
    const auto* s = filtered[0];
    std::string f = s->id.substr(0, s->id.find("::"));
    return {s, std::move(f)};
  }
  if (filtered.size() > 1) {
    nlohmann::json candidates = nlohmann::json::array();
    for (size_t i = 0; i < std::min<size_t>(filtered.size(), 10); ++i) {
      candidates.push_back({
          {"id", filtered[i]->id},
          {"signature", filtered[i]->sig.canonical(filtered[i]->name)}
      });
    }
    throw ToolError("SYMBOL_AMBIGUOUS", "multiple candidates for '" + std::string(ref) + "'", {{"candidates", candidates}});
  }

  // 4. Search by bare name
  auto name_matches = fact_store.find_symbols_by_name(target_name);
  filtered.clear();
  for (const auto* s : name_matches) {
    if (target_file.empty() || s->id.starts_with(target_file + "::")) {
      filtered.push_back(s);
    }
  }

  if (filtered.size() == 1) {
    const auto* s = filtered[0];
    std::string f = s->id.substr(0, s->id.find("::"));
    return {s, std::move(f)};
  }
  if (filtered.size() > 1) {
    nlohmann::json candidates = nlohmann::json::array();
    for (size_t i = 0; i < std::min<size_t>(filtered.size(), 10); ++i) {
      candidates.push_back({
          {"id", filtered[i]->id},
          {"signature", filtered[i]->sig.canonical(filtered[i]->name)}
      });
    }
    throw ToolError("SYMBOL_AMBIGUOUS", "multiple candidates for '" + std::string(ref) + "'", {{"candidates", candidates}});
  }

  // 5. Not found -> find closest by Damerau-Levenshtein distance
  struct ScoredSuggestion {
    std::string id;
    size_t dist;
  };
  std::vector<ScoredSuggestion> suggestions;

  for (const auto& file_path : fact_store.all_files()) {
    const auto* facts = fact_store.get(file_path);
    if (!facts) continue;
    for (const auto& sym : facts->symbols) {
      if (sym.name == "<module>") continue;
      size_t d1 = core::damerau_levenshtein(target_name, sym.name);
      size_t d2 = core::damerau_levenshtein(target_name, sym.qualname);
      size_t min_d = std::min(d1, d2);
      if (min_d <= 4) {
        suggestions.push_back({sym.id, min_d});
      }
    }
  }

  std::sort(suggestions.begin(), suggestions.end(), [](const auto& a, const auto& b) {
    return a.dist < b.dist;
  });

  nlohmann::json did_you_mean = nlohmann::json::array();
  for (size_t i = 0; i < std::min<size_t>(suggestions.size(), 5); ++i) {
    did_you_mean.push_back(suggestions[i].id);
  }

  std::string msg = "no symbol '" + std::string(ref) + "' found";
  if (!target_file.empty()) {
    msg += " in " + target_file;
  }
  throw ToolError("SYMBOL_NOT_FOUND", std::move(msg), {{"did_you_mean", did_you_mean}});
}

} // namespace

void register_read_tools(ToolRegistry& reg) {
  // ── 1. search_symbols ──────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "search_symbols",
      .title = "Search symbols",
      .description = "Find functions/classes by name or words (fuzzy, prefix). Use before calling or editing code you haven't seen.",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false},
          {"required", {"query"}},
          {"properties", {
              {"query", {{"type", "string"}, {"minLength", 1}, {"maxLength", 200}}},
              {"kind", {{"type", "string"}, {"enum", {"any", "function", "class"}}, {"default", "any"}}},
              {"path_prefix", {{"type", "string"}, {"maxLength", 500}, {"default", ""}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"default", 10}}}
          }}
      },
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string query = args["query"].get<std::string>();
        std::string kind = args.value("kind", "any");
        std::string prefix = args.value("path_prefix", "");
        size_t limit = args.value("limit", 10);

        std::vector<nlohmann::json> results;
        std::unordered_set<std::string> seen_ids;

        // FactStore exact matches (score 100)
        auto name_matches = ctx.engine.fact_store().find_symbols_by_name(query);
        for (const auto* s : name_matches) {
          if (s->name == "<module>") continue;
          std::string file = s->id.substr(0, s->id.find("::"));
          if (!prefix.empty() && !file.starts_with(prefix)) continue;

          std::string skind = (s->kind == lang::SymbolKind::Class || s->kind == lang::SymbolKind::Interface) ? "class" : "function";
          if (kind != "any" && kind != skind) continue;

          if (seen_ids.insert(s->id).second) {
            results.push_back({
                {"id", s->id},
                {"kind", skind},
                {"signature", s->sig.canonical(s->name)},
                {"file", file},
                {"start_line", s->lines.start},
                {"doc", s->doc},
                {"score", 100}
            });
          }
        }

        // FalkorDB fulltext query if available
        if (ctx.engine.client().ping()) {
          std::string project_graph = ctx.engine.config().project_graph();
          if (kind == "any" || kind == "function") {
            try {
              auto rows = ctx.engine.client().rows(project_graph, {
                  "search_fn",
                  db::Q_SEARCH_FUNCTIONS,
                  {{"q", query}, {"limit", static_cast<int64_t>(limit)}},
                  true
              });
              for (const auto& r : rows) {
                if (r.contains("id") && seen_ids.insert(r["id"].get<std::string>()).second) {
                  std::string f = r.value("file", "");
                  if (!prefix.empty() && !f.starts_with(prefix)) continue;
                  results.push_back(r);
                }
              }
            } catch (...) {}
          }
          if (kind == "any" || kind == "class") {
            try {
              auto rows = ctx.engine.client().rows(project_graph, {
                  "search_cls",
                  db::Q_SEARCH_CLASSES,
                  {{"q", query}, {"limit", static_cast<int64_t>(limit)}},
                  true
              });
              for (const auto& r : rows) {
                if (r.contains("id") && seen_ids.insert(r["id"].get<std::string>()).second) {
                  std::string f = r.value("file", "");
                  if (!prefix.empty() && !f.starts_with(prefix)) continue;
                  results.push_back(r);
                }
              }
            } catch (...) {}
          }
        }

        if (results.size() > limit) {
          results.resize(limit);
        }

        return {
            {"results", results},
            {"total", results.size()}
        };
      }
  });

  // ── 2. get_file_outline ────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "get_file_outline",
      .title = "File outline",
      .description = "Compact outline of a file: imports, classes with methods, functions with signatures and line ranges, fan-in. Cheaper than reading the file.",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false},
          {"required", {"file"}},
          {"properties", {
              {"file", {{"type", "string"}, {"minLength", 1}, {"maxLength", 500}}}
          }}
      },
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string file = args["file"].get<std::string>();
        ctx.engine.indexer().ensure_fresh(file);

        // Try DB first
        if (ctx.engine.client().ping()) {
          try {
            auto outline = ctx.engine.client().one(
                ctx.engine.config().project_graph(),
                {"file_outline", db::Q_FILE_OUTLINE, {{"file", file}}, true});
            if (outline.is_object() && outline.contains("file")) {
              return outline;
            }
          } catch (...) {}
        }

        // Fallback to FactStore in RAM
        const auto* facts = ctx.engine.fact_store().get(file);
        if (!facts) {
          throw ToolError("FILE_NOT_FOUND", "file '" + file + "' not found in workspace");
        }

        nlohmann::json imports = nlohmann::json::array();
        for (const auto& imp : facts->imports) {
          nlohmann::json names = nlohmann::json::array();
          for (const auto& pair : imp.names) {
            names.push_back(pair.first);
          }
          imports.push_back({
              {"file", imp.spec},
              {"names", names},
              {"line", imp.line}
          });
        }

        nlohmann::json classes = nlohmann::json::array();
        nlohmann::json functions = nlohmann::json::array();

        for (const auto& s : facts->symbols) {
          if (s.name == "<module>") continue;
          if (s.kind == lang::SymbolKind::Class || s.kind == lang::SymbolKind::Interface) {
            nlohmann::json methods = nlohmann::json::array();
            for (const auto& m : facts->symbols) {
              if (m.class_qualname == s.qualname && m.kind == lang::SymbolKind::Method) {
                methods.push_back({
                    {"name", m.name},
                    {"signature", m.sig.canonical(m.name)},
                    {"start_line", m.lines.start},
                    {"end_line", m.lines.end}
                });
              }
            }
            classes.push_back({
                {"id", s.id},
                {"name", s.name},
                {"kind", lang::symbol_kind_name(s.kind)},
                {"bases", s.bases},
                {"start_line", s.lines.start},
                {"end_line", s.lines.end},
                {"methods", methods}
            });
          } else if (s.kind == lang::SymbolKind::Function) {
            functions.push_back({
                {"id", s.id},
                {"name", s.name},
                {"signature", s.sig.canonical(s.name)},
                {"start_line", s.lines.start},
                {"end_line", s.lines.end},
                {"fan_in", 0}
            });
          }
        }

        return {
            {"file", facts->file},
            {"language", lang::LanguageRegistry::get().info(facts->lang).name},
            {"line_count", facts->line_count},
            {"parse_ok", facts->parse_ok},
            {"hash", facts->hash},
            {"imports", imports},
            {"imported_by", nlohmann::json::array()},
            {"classes", classes},
            {"functions", functions}
        };
      }
  });

  // ── 3. read_symbol ─────────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "read_symbol",
      .title = "Read symbol with context",
      .description = "Exact source of one function/class plus its callers, callees, change history, pitfalls and attached memories. Use before editing it; the returned source is what replace_symbol expects.",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false},
          {"required", {"symbol"}},
          {"properties", {
              {"symbol", {{"type", "string"}, {"minLength", 1}, {"maxLength", 500}}},
              {"file", {{"type", "string"}, {"maxLength", 500}, {"default", ""}}},
              {"include", {
                  {"type", "array"},
                  {"items", {{"type", "string"}, {"enum", {"source", "callers", "callees", "history", "pitfalls", "memories"}}}},
                  {"default", {"source", "callers", "callees", "history", "pitfalls", "memories"}}
              }},
              {"max_callers", {{"type", "integer"}, {"minimum", 0}, {"maximum", 200}, {"default", 20}}}
          }}
      },
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string symbol_ref = args["query"].is_string() ? args["query"].get<std::string>() : args["symbol"].get<std::string>();
        std::string file_filter = args.value("file", "");
        size_t max_callers = args.value("max_callers", 20);

        auto resolved = resolve_symbol_ref(ctx.engine.fact_store(), symbol_ref, file_filter);
        const auto* sym = resolved.sym;
        const std::string& file = resolved.file;

        // Ensure freshness
        ctx.engine.indexer().ensure_fresh(file);

        // Re-resolve in case index refreshed
        resolved = resolve_symbol_ref(ctx.engine.fact_store(), sym->id, file);
        sym = resolved.sym;

        // Read source bytes from disk
        auto full_path = ctx.engine.config().root / platform::from_utf8(file);
        std::ifstream in(full_path, std::ios::binary);
        if (!in) {
          throw ToolError("FILE_NOT_FOUND", "failed to read file: " + file);
        }

        std::string file_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        // Hash verify
        std::string disk_hash = core::hash_bytes(file_content);
        const auto* facts = ctx.engine.fact_store().get(file);
        if (facts && disk_hash != facts->hash) {
          ctx.engine.indexer().reindex({file});
          resolved = resolve_symbol_ref(ctx.engine.fact_store(), sym->id, file);
          sym = resolved.sym;
        }

        std::string source;
        if (sym->def_range.start <= file_content.size() && sym->def_range.end <= file_content.size()) {
          source = file_content.substr(sym->def_range.start, sym->def_range.end - sym->def_range.start);
        }

        nlohmann::json params_arr = nlohmann::json::array();
        for (const auto& p : sym->sig.params) {
          params_arr.push_back({
              {"name", p.name},
              {"kind", lang::param_kind_name(p.kind)},
              {"has_default", p.has_default},
              {"type", p.type}
          });
        }

        nlohmann::json result = {
            {"id", sym->id},
            {"kind", lang::symbol_kind_name(sym->kind)},
            {"file", file},
            {"start_line", sym->lines.start},
            {"end_line", sym->lines.end},
            {"signature", sym->sig.canonical(sym->name)},
            {"params", params_arr},
            {"file_hash", facts ? facts->hash : disk_hash},
            {"criticality", {{"tier", "low"}, {"pr_pct", 0.0}, {"pagerank", 0.0}}},
            {"source", source},
            {"callers", nlohmann::json::array()},
            {"caller_count", 0},
            {"callees", nlohmann::json::array()},
            {"history", nlohmann::json::array()},
            {"pitfalls", nlohmann::json::array()},
            {"memories", nlohmann::json::array()}
        };

        // Query graph database for callers/callees/history/pitfalls/memories
        if (ctx.engine.client().ping()) {
          try {
            std::string project_graph = ctx.engine.config().project_graph();
            if (sym->kind == lang::SymbolKind::Class || sym->kind == lang::SymbolKind::Interface) {
              auto ctx_row = ctx.engine.client().one(
                  project_graph,
                  {"read_cls_ctx", db::Q_READ_CLASS_CONTEXT, {{"id", sym->id}, {"limit", static_cast<int64_t>(max_callers)}}, true});
              if (ctx_row.is_object()) {
                if (ctx_row.contains("history")) result["history"] = ctx_row["history"];
                if (ctx_row.contains("pitfalls")) result["pitfalls"] = ctx_row["pitfalls"];
              }
            } else {
              auto ctx_row = ctx.engine.client().one(
                  project_graph,
                  {"read_sym_ctx", db::Q_READ_SYMBOL_CONTEXT, {{"id", sym->id}, {"limit", static_cast<int64_t>(max_callers)}}, true});
              if (ctx_row.is_object()) {
                if (ctx_row.contains("callers")) result["callers"] = ctx_row["callers"];
                if (ctx_row.contains("caller_count")) result["caller_count"] = ctx_row["caller_count"];
                if (ctx_row.contains("callees")) result["callees"] = ctx_row["callees"];
                if (ctx_row.contains("history")) result["history"] = ctx_row["history"];
                if (ctx_row.contains("pitfalls")) result["pitfalls"] = ctx_row["pitfalls"];
                if (ctx_row.contains("memories")) result["memories"] = ctx_row["memories"];
              }
            }
          } catch (...) {}
        }

        return result;
      }
  });

  // ── 4. get_impact ──────────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "get_impact",
      .title = "Impact analysis",
      .description = "Transitive callers of a symbol (who breaks if it changes), grouped by depth, plus affected files. Call before changing a signature or deleting.",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false},
          {"required", {"symbol"}},
          {"properties", {
              {"symbol", {{"type", "string"}, {"minLength", 1}, {"maxLength", 500}}},
              {"file", {{"type", "string"}, {"maxLength", 500}, {"default", ""}}},
              {"depth", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5}, {"default", 3}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 100}}}
          }}
      },
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string symbol_ref = args["symbol"].get<std::string>();
        std::string file_filter = args.value("file", "");
        int depth = args.value("depth", 3);
        int64_t limit = args.value("limit", 100);

        auto resolved = resolve_symbol_ref(ctx.engine.fact_store(), symbol_ref, file_filter);
        std::string sym_id = resolved.sym->id;

        nlohmann::json callers = nlohmann::json::array();
        nlohmann::json files = nlohmann::json::array();
        std::string risk = "low";

        if (ctx.engine.client().ping()) {
          try {
            std::string project_graph = ctx.engine.config().project_graph();
            callers = ctx.engine.client().rows(
                project_graph,
                {"impact", db::Q_IMPACT(depth), {{"id", sym_id}, {"limit", limit}}, true});
            auto files_row = ctx.engine.client().one(
                project_graph,
                {"impact_files", db::Q_IMPACT_FILES(depth), {{"id", sym_id}}, true});
            if (files_row.is_array()) {
              files = files_row;
            }

            // Calculate risk
            bool has_high_caller = false;
            bool has_med_caller = false;
            for (const auto& c : callers) {
              int d = c.value("depth", 1);
              std::string crit = c.value("criticality", "low");
              if (d == 1 && crit == "high") has_high_caller = true;
              if (crit == "medium") has_med_caller = true;
            }

            if (has_high_caller || files.size() >= 5) {
              risk = "high";
            } else if (has_med_caller || files.size() >= 2) {
              risk = "medium";
            }
          } catch (...) {}
        } else {
          // Offline fallback from in-memory FactStore and Resolver
          std::string target_file = resolved.file;
          auto dependents = ctx.engine.resolver().dependents_of(target_file);
          dependents.push_back(target_file);

          for (const auto& dep_f : dependents) {
            auto dep_calls = ctx.engine.resolver().resolve_file(ctx.engine.fact_store(), dep_f);
            for (const auto& rc : dep_calls) {
              if (rc.target_id == sym_id || rc.call_text.find(resolved.sym->name) != std::string::npos) {
                callers.push_back({
                    {"caller", rc.caller_id},
                    {"file", dep_f},
                    {"line", rc.line},
                    {"col", rc.col},
                    {"call", rc.call_text},
                    {"depth", 1},
                    {"confidence", rc.confidence}
                });
                bool file_already_present = false;
                for (const auto& existing_f : files) {
                  if (existing_f == dep_f) { file_already_present = true; break; }
                }
                if (!file_already_present) {
                  files.push_back(dep_f);
                }
              }
            }
          }

          if (files.size() >= 5) {
            risk = "high";
          } else if (files.size() >= 2) {
            risk = "medium";
          }
        }

        return {
            {"id", sym_id},
            {"depth", depth},
            {"criticality", {{"tier", "low"}}},
            {"risk", risk},
            {"callers", callers},
            {"files", files},
            {"truncated", false}
        };
      }
  });

  // ── 5. get_module_map ──────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "get_module_map",
      .title = "Module map",
      .description = "Architecture overview: files under a path with their import edges, the most central functions (PageRank), bridge modules (betweenness) and isolated files (connected components).",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false},
          {"properties", {
              {"path_prefix", {{"type", "string"}, {"maxLength", 500}, {"default", ""}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 200}}},
              {"include_hotspots", {{"type", "boolean"}, {"default", true}}}
          }}
      },
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string prefix = args.value("path_prefix", "");
        int64_t limit = args.value("limit", 200);
        bool include_hotspots = args.value("include_hotspots", true);

        nlohmann::json files = nlohmann::json::array();
        nlohmann::json hotspots = nlohmann::json::array();

        if (ctx.engine.client().ping()) {
          try {
            std::string project_graph = ctx.engine.config().project_graph();
            files = ctx.engine.client().rows(
                project_graph,
                {"module_map", db::Q_MODULE_MAP, {{"prefix", prefix}, {"limit", limit}}, true});
            if (include_hotspots) {
              hotspots = ctx.engine.client().rows(
                  project_graph,
                  {"hotspots", db::Q_HOTSPOTS, {{"prefix", prefix}, {"limit", int64_t(10)}}, true});
            }
          } catch (...) {}
        } else {
          // FactStore fallback
          for (const auto& f : ctx.engine.fact_store().all_files()) {
            if (!prefix.empty() && !f.starts_with(prefix)) continue;
            const auto* facts = ctx.engine.fact_store().get(f);
            if (!facts) continue;
            nlohmann::json imps = nlohmann::json::array();
            for (const auto& imp : facts->imports) {
              imps.push_back(imp.spec);
            }
            files.push_back({
                {"file", f},
                {"language", lang::LanguageRegistry::get().info(facts->lang).name},
                {"symbols", facts->symbols.size()},
                {"imports", imps},
                {"imported_by_count", 0}
            });
          }
        }

        return {
            {"files", files},
            {"hotspots", hotspots},
            {"bridges", nlohmann::json::array()},
            {"islands", nlohmann::json::array()},
            {"truncated", false}
        };
      }
  });
}

} // namespace gs::mcp
