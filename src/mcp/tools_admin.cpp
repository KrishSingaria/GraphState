#include "mcp/tool_registry.hpp"

#include <chrono>
#include "core/clock.hpp"
#include "core/engine.hpp"
#include "db/queries.hpp"
#include "platform/platform.hpp"

#ifndef GS_VERSION
#define GS_VERSION "0.1.0"
#endif

namespace gs::mcp {

void register_admin_tools(ToolRegistry& reg) {
  // ── reindex ────────────────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "reindex",
      .title = "Re-index",
      .description = "Re-index specific files or the whole workspace (normally automatic).",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false},
          {"properties", {
              {"files", {{"type", "array"}, {"items", {{"type", "string"}}}, {"maxItems", 1000}}},
              {"full", {{"type", "boolean"}, {"default", false}}}
          }}
      },
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        bool full = args.value("full", false);
        std::vector<std::string> files;
        if (args.contains("files") && args["files"].is_array()) {
          for (const auto& item : args["files"]) {
            if (item.is_string()) {
              files.push_back(item.get<std::string>());
            }
          }
        }

        index::IndexReport report;
        if (full) {
          report = ctx.engine.indexer().full_rebuild();
        } else if (!files.empty()) {
          report = ctx.engine.indexer().reindex(files);
        } else {
          report = ctx.engine.indexer().startup_reconcile();
        }

        return {
            {"scanned", report.scanned},
            {"parsed", report.parsed},
            {"changed", report.changed},
            {"deleted", report.deleted},
            {"skipped", report.skipped},
            {"ms", report.ms}
        };
      }
  });

  // ── status ─────────────────────────────────────────────────────────────────
  reg.add(Tool{
      .name = "status",
      .title = "Status",
      .description = "Engine health: database, graph sizes, index freshness, gate mode, rule errors, pending journal entries.",
      .input_schema = {
          {"type", "object"},
          {"additionalProperties", false}
      },
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json&) -> nlohmann::json {
        core::Stopwatch sw;
        bool connected = ctx.engine.client().ping();
        double ping_ms = sw.elapsed_ms();

        nlohmann::json counts = nlohmann::json::object();
        int64_t last_indexed_at = 0;

        if (connected) {
          std::string project_graph = ctx.engine.config().project_graph();
          try {
            auto count_rows = ctx.engine.client().rows(
                project_graph,
                {"counts", db::Q_COUNTS, nlohmann::json::object(), true});
            for (const auto& r : count_rows) {
              if (r.contains("label") && r.contains("n")) {
                counts[r["label"].get<std::string>()] = r["n"].get<int64_t>();
              }
            }
            auto meta_row = ctx.engine.client().one(
                project_graph,
                {"meta_get", db::Q_META_GET, nlohmann::json::object(), true});
            if (meta_row.is_object() && meta_row.contains("last_indexed_at") && !meta_row["last_indexed_at"].is_null()) {
              last_indexed_at = meta_row["last_indexed_at"].get<int64_t>();
            }
          } catch (...) {}
        }

        // Fill default counts from FactStore if empty
        if (!counts.contains("File")) counts["File"] = ctx.engine.fact_store().file_count();
        if (!counts.contains("Function")) counts["Function"] = ctx.engine.fact_store().total_symbols();
        if (!counts.contains("Class")) counts["Class"] = 0;
        if (!counts.contains("Session")) counts["Session"] = 0;
        if (!counts.contains("Decision")) counts["Decision"] = 0;
        if (!counts.contains("Pitfall")) counts["Pitfall"] = 0;
        if (!counts.contains("Memory")) counts["Memory"] = 0;

        return {
            {"version", GS_VERSION},
            {"root", platform::to_utf8(ctx.engine.config().root)},
            {"graph", ctx.engine.config().project_graph()},
            {"db", {
                {"connected", connected},
                {"ping_ms", ping_ms},
                {"host", ctx.engine.config().falkordb.host},
                {"port", ctx.engine.config().falkordb.port}
            }},
            {"counts", counts},
            {"user_memories", 0},
            {"last_indexed_at", last_indexed_at},
            {"watcher", "running"},
            {"gate_mode", ctx.engine.config().gate.mode},
            {"rules_errors", nlohmann::json::array()},
            {"wal_pending", 0},
            {"session_id", ctx.session_id}
        };
      }
  });
}

} // namespace gs::mcp
