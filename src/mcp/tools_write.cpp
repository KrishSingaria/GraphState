#include "mcp/tool_registry.hpp"

#include <iomanip>
#include <sstream>
#include "core/engine.hpp"
#include "gate/gate.hpp"
#include "gate/patch.hpp"
#include "memory/session_ctx.hpp"

namespace gs::mcp {

void register_write_tools(ToolRegistry& reg) {
  reg.add(Tool{
      .name = "propose_patch",
      .title = "Propose a gated patch",
      .description = "The ONLY way to change code. Applies one or more edits (across files) atomically after verification: syntax, signature changes vs all callers (blast radius), calls to missing/incompatible functions, and quality rules. Returns APPLIED (written to the working tree), VALIDATED (dry_run) or REJECTED (nothing written) with exact locations and fix hints. Include every caller update in the same patch when changing a signature. While a changeset is open, cross-file problems are tracked as obligations instead of rejections.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "required": ["intent", "reasoning", "edits"],
        "properties": {
          "intent": {"type": "string", "minLength": 1, "maxLength": 500},
          "reasoning": {"type": "string", "minLength": 1, "maxLength": 4000},
          "dry_run": {"type": "boolean", "default": false},
          "base_hashes": {"type": "object", "additionalProperties": {"type": "string"}},
          "edits": {
            "type": "array",
            "minItems": 1,
            "maxItems": 200,
            "items": {
              "type": "object",
              "additionalProperties": false,
              "required": ["op", "file"],
              "properties": {
                "op": {"type": "string", "enum": ["replace_symbol", "insert_symbol", "delete_symbol", "replace_text", "create_file", "delete_file", "move_file"]},
                "file": {"type": "string", "minLength": 1, "maxLength": 500},
                "symbol": {"type": "string", "maxLength": 500},
                "code": {"type": "string", "maxLength": 1048576},
                "anchor": {
                  "type": "object",
                  "additionalProperties": false,
                  "properties": {
                    "after": {"type": "string"},
                    "before": {"type": "string"},
                    "into_class": {"type": "string"},
                    "at_start": {"type": "boolean"}
                  }
                },
                "old_text": {"type": "string", "maxLength": 1048576},
                "new_text": {"type": "string", "maxLength": 1048576},
                "content": {"type": "string", "maxLength": 1048576},
                "to": {"type": "string", "maxLength": 500}
              }
            }
          }
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", true},
          {"idempotentHint", false},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        gate::PatchRequest req = gate::parse_patch_request(args);
        memory::SessionCtx session{ctx.session_id, "", "mcp"};
        gate::PatchResult res = ctx.engine.gate().propose(req, session);
        return gate::to_json(res);
      },
      .render = [](const nlohmann::json& j) -> std::string {
        std::string status = j.value("status", "REJECTED");
        std::string patch_id = j.value("patch_id", "");

        if (status == "APPLIED") {
          size_t files_count = j.contains("changes") ? j["changes"].size() : 0;
          size_t symbols_count = 0;
          if (j.contains("changes") && j["changes"].is_array()) {
            for (const auto& c : j["changes"]) {
              if (c.contains("symbols_added")) symbols_count += c["symbols_added"].size();
              if (c.contains("symbols_signature_changed")) symbols_count += c["symbols_signature_changed"].size();
              if (c.contains("symbols_body_changed")) symbols_count += c["symbols_body_changed"].size();
            }
          }
          double total_ms = 0.0;
          if (j.contains("timings_ms") && j["timings_ms"].contains("total")) {
            total_ms = j["timings_ms"]["total"].get<double>();
          }

          std::ostringstream oss;
          oss << "APPLIED " << files_count << " files, " << symbols_count << " symbols changed (patch "
              << patch_id << ") in " << std::fixed << std::setprecision(1) << total_ms << " ms";
          return oss.str();
        }

        if (status == "VALIDATED") {
          return "VALIDATED: patch " + patch_id + " dry-run passed";
        }

        // REJECTED
        std::string failed_phase = j.value("failed_phase", "PREFLIGHT");
        size_t errors_count = j.contains("violations") ? j["violations"].size() : 0;
        size_t warnings_count = j.contains("warnings") ? j["warnings"].size() : 0;

        return "REJECTED at " + failed_phase + ": " + std::to_string(errors_count) + " errors, " +
               std::to_string(warnings_count) + " warnings (patch " + patch_id + ")";
      }
  });

  reg.add(Tool{
      .name = "changeset",
      .title = "Changeset (incremental work)",
      .description = "Group several patches into one unit of work for building new code or multi-step refactors. begin: open (cross-file problems become obligations instead of rejections). status: list obligations. finalize: close when no obligations remain. abandon: restore every file the changeset touched.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "required": ["action"],
        "properties": {
          "action": {"type": "string", "enum": ["begin", "status", "finalize", "abandon"]},
          "intent": {"type": "string", "minLength": 1, "maxLength": 500}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", true},
          {"idempotentHint", false},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext&, const nlohmann::json&) -> nlohmann::json {
        throw ToolError("NOT_IMPLEMENTED", "changeset is implemented in milestone M5b");
      }
  });
}

} // namespace gs::mcp
