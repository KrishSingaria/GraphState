#include <sstream>
#include "core/engine.hpp"
#include "mcp/tool_registry.hpp"
#include "memory/briefing.hpp"
#include "memory/memory_store.hpp"
#include "memory/sessions.hpp"

namespace gs::mcp {

void register_memory_tools(ToolRegistry& reg) {
  // 1. get_briefing
  reg.add(Tool{
      .name = "get_briefing",
      .title = "Session briefing",
      .description = "Call FIRST in every session. Returns user preferences, project conventions, unfinished work from earlier sessions, known pitfalls, recent changes and enforced rules. Pass your goal to record it and get related memories.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "goal": {"type": "string", "minLength": 1, "maxLength": 500, "description": "What you are about to do, one sentence."}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string goal = args.value("goal", "");
        auto res = memory::generate_briefing(goal, ctx.session, ctx.engine);
        return res.structured_content;
      },
      .render = [](const nlohmann::json& structured) -> std::string {
        std::string ws_name = structured.value("workspace", nlohmann::json::object()).value("root", "workspace");
        size_t last_slash = ws_name.find_last_of("/\\");
        if (last_slash != std::string::npos) ws_name = ws_name.substr(last_slash + 1);

        std::string session_id = structured.value("session_id", "");
        std::ostringstream ss;
        ss << "# GraphState briefing — " << ws_name << " (session " << session_id << ")\n";

        if (structured.contains("user_memories") && structured["user_memories"].is_array() && !structured["user_memories"].empty()) {
          ss << "## You (user preferences)\n";
          for (const auto& m : structured["user_memories"]) {
            ss << "- [preference] " << m.value("content", "") << " (" << m.value("id", "") << ")\n";
          }
        }

        if (structured.contains("project_memories") && structured["project_memories"].is_array() && !structured["project_memories"].empty()) {
          ss << "## This project\n";
          for (const auto& m : structured["project_memories"]) {
            ss << "- [" << m.value("kind", "convention") << "] " << m.value("content", "") << " (" << m.value("id", "") << ")\n";
          }
        }

        if (structured.contains("open_changeset") && structured["open_changeset"].is_object()) {
          const auto& cs = structured["open_changeset"];
          ss << "## Open changeset (continue it)\n";
          ss << "- " << cs.value("id", "") << " \"" << cs.value("intent", "") << "\" — "
             << cs.value("open_obligations", 0) << " obligations\n";
        }

        if (structured.contains("open_intents") && structured["open_intents"].is_array() && !structured["open_intents"].empty()) {
          ss << "## Unfinished work\n";
          for (const auto& oi : structured["open_intents"]) {
            ss << "- \"" << oi.value("goal", "") << "\" — previous session " << oi.value("session_status", "OPEN") << "\n";
          }
        }

        if (structured.contains("top_pitfalls") && structured["top_pitfalls"].is_array() && !structured["top_pitfalls"].empty()) {
          ss << "## Known pitfalls (most frequent first)\n";
          for (const auto& p : structured["top_pitfalls"]) {
            std::string sym = p.value("symbol_id", "");
            if (sym.empty()) sym = p.value("file", "");
            ss << "- ×" << p.value("count", 1) << " " << p.value("code", "") << " " << sym << " — " << p.value("lesson", "") << "\n";
          }
        }

        if (structured.contains("recent_sessions") && structured["recent_sessions"].is_array() && !structured["recent_sessions"].empty()) {
          ss << "## Recent sessions\n";
          for (const auto& s : structured["recent_sessions"]) {
            ss << "- " << s.value("client", "agent") << " " << s.value("status", "ENDED") << ": " << s.value("summary", "")
               << " (" << s.value("applied", 0) << " applied, " << s.value("rejected", 0) << " rejected)\n";
          }
        }

        if (structured.contains("hot_symbols") && structured["hot_symbols"].is_array() && !structured["hot_symbols"].empty()) {
          ss << "## Recently changed hot spots\n";
          for (const auto& hs : structured["hot_symbols"]) {
            ss << "- " << hs.value("id", "") << " (" << hs.value("changes", 1) << " changes)\n";
          }
        }

        if (structured.contains("rules") && structured["rules"].is_array() && !structured["rules"].empty()) {
          ss << "## Enforced rules\n- ";
          for (size_t i = 0; i < structured["rules"].size(); ++i) {
            if (i > 0) ss << ", ";
            ss << structured["rules"][i].value("id", "");
          }
          ss << "\n";
        }

        if (structured.contains("workspace") && structured["workspace"].is_object()) {
          const auto& ws = structured["workspace"];
          ss << "## Workspace\n- " << ws.value("files", 0) << " files · "
             << ws.value("functions", 0) << " functions · " << ws.value("classes", 0) << " classes\n";
        }

        return ss.str();
      }
  });

  // 2. remember
  reg.add(Tool{
      .name = "remember",
      .title = "Remember",
      .description = "Save a durable memory: a user preference (follows the user across projects), a project convention, a fact, a todo or a warning. Attach it to symbols/files with 'about'. Never store secrets.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "required": ["content", "kind"],
        "properties": {
          "content": {"type": "string", "minLength": 1, "maxLength": 2000},
          "kind": {"type": "string", "enum": ["preference", "convention", "fact", "todo", "warning"]},
          "scope": {"type": "string", "enum": ["user", "project"]},
          "about": {"type": "array", "items": {"type": "string", "maxLength": 500}, "maxItems": 20},
          "tags": {"type": "array", "items": {"type": "string", "maxLength": 50}, "maxItems": 10},
          "importance": {"type": "integer", "minimum": 1, "maximum": 3, "default": 2},
          "supersedes": {"type": "string", "maxLength": 20}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        memory::RememberRequest req;
        req.content = args.value("content", "");
        req.kind = args.value("kind", "");
        req.scope = args.value("scope", "");
        if (args.contains("about") && args["about"].is_array()) {
          for (const auto& a : args["about"]) {
            if (a.is_string()) req.about.push_back(a.get<std::string>());
          }
        }
        if (args.contains("tags") && args["tags"].is_array()) {
          for (const auto& t : args["tags"]) {
            if (t.is_string()) req.tags.push_back(t.get<std::string>());
          }
        }
        req.importance = args.value("importance", 2);
        req.supersedes = args.value("supersedes", "");

        auto res = ctx.engine.memory_store().remember(req, ctx.session);
        return {
            {"id", res.id},
            {"scope", res.scope},
            {"deduplicated", res.deduplicated},
            {"anchors", res.anchors}
        };
      },
      .render = [](const nlohmann::json& structured) -> std::string {
        std::string id = structured.value("id", "");
        std::string sc = structured.value("scope", "project");
        bool dedup = structured.value("deduplicated", false);
        if (dedup) {
          return "Memory updated (deduplicated: " + id + ", scope: " + sc + ")";
        }
        return "Saved memory " + id + " (scope: " + sc + ")";
      }
  });

  // 3. recall
  reg.add(Tool{
      .name = "recall",
      .title = "Recall memory",
      .description = "Search past memories, decisions, pitfalls, intents and sessions by words, or everything attached to a symbol/file.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "query": {"type": "string", "maxLength": 500},
          "types": {"type": "array", "items": {"type": "string", "enum": ["memory", "decision", "pitfall", "session", "intent"]}},
          "scope": {"type": "string", "enum": ["all", "project", "user"], "default": "all"},
          "about": {"type": "string", "maxLength": 500},
          "limit": {"type": "integer", "minimum": 1, "maximum": 50, "default": 10}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        memory::RecallRequest req;
        req.query = args.value("query", "");
        if (args.contains("types") && args["types"].is_array()) {
          for (const auto& t : args["types"]) {
            if (t.is_string()) req.types.push_back(t.get<std::string>());
          }
        }
        req.scope = args.value("scope", "all");
        req.about = args.value("about", "");
        req.limit = args.value("limit", 10);

        auto res = ctx.engine.memory_store().recall(req, ctx.session);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : res.results) {
          nlohmann::json obj = {
              {"type", item.type},
              {"id", item.id},
              {"score", item.score},
              {"text", item.text}
          };
          for (auto it = item.meta.begin(); it != item.meta.end(); ++it) {
            obj[it.key()] = it.value();
          }
          arr.push_back(std::move(obj));
        }
        return {{"results", arr}};
      },
      .render = [](const nlohmann::json& structured) -> std::string {
        if (!structured.contains("results") || !structured["results"].is_array() || structured["results"].empty()) {
          return "No memories found.";
        }
        std::ostringstream ss;
        ss << "Found " << structured["results"].size() << " match(es):\n";
        for (const auto& r : structured["results"]) {
          ss << "- [" << r.value("type", "") << "] " << r.value("text", "")
             << " (" << r.value("id", "") << ", score: " << r.value("score", 0.0) << ")\n";
        }
        return ss.str();
      }
  });

  // 4. recall_pitfalls
  reg.add(Tool{
      .name = "recall_pitfalls",
      .title = "Known pitfalls",
      .description = "Known mistakes recorded from rejected patches, most frequent first. Filter by symbol, file or violation code.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "symbol": {"type": "string", "maxLength": 500},
          "file": {"type": "string", "maxLength": 500},
          "code": {"type": "string", "maxLength": 64},
          "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", true},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        memory::RecallPitfallsRequest req;
        req.symbol = args.value("symbol", "");
        req.file = args.value("file", "");
        req.code = args.value("code", "");
        req.limit = args.value("limit", 20);

        auto res = ctx.engine.memory_store().recall_pitfalls(req);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : res.pitfalls) {
          arr.push_back({
              {"key", p.key},
              {"code", p.code},
              {"rule_id", p.rule_id},
              {"symbol_id", p.symbol_id},
              {"file", p.file},
              {"lesson", p.lesson},
              {"count", p.count},
              {"resolved_count", p.resolved_count},
              {"first_seen", p.first_seen},
              {"last_seen", p.last_seen},
              {"last_resolved", p.last_resolved},
              {"unresolved", p.unresolved}
          });
        }
        return {{"pitfalls", arr}};
      },
      .render = [](const nlohmann::json& structured) -> std::string {
        if (!structured.contains("pitfalls") || !structured["pitfalls"].is_array() || structured["pitfalls"].empty()) {
          return "No pitfalls found.";
        }
        std::ostringstream ss;
        ss << "Known pitfalls (" << structured["pitfalls"].size() << "):\n";
        for (const auto& p : structured["pitfalls"]) {
          std::string sym = p.value("symbol_id", "");
          if (sym.empty()) sym = p.value("file", "");
          ss << "- ×" << p.value("count", 1) << " " << p.value("code", "")
             << " " << sym << ": " << p.value("lesson", "") << "\n";
        }
        return ss.str();
      }
  });

  // 5. end_session
  reg.add(Tool{
      .name = "end_session",
      .title = "End session",
      .description = "Call before finishing: store a short summary of what was done and what remains, and mark intents done/abandoned.",
      .input_schema = nlohmann::json::parse(R"json({
        "type": "object",
        "additionalProperties": false,
        "required": ["summary"],
        "properties": {
          "summary": {"type": "string", "minLength": 1, "maxLength": 2000},
          "intents_done": {"type": "array", "items": {"type": "string"}},
          "intents_abandoned": {"type": "array", "items": {"type": "string"}}
        }
      })json"),
      .annotations = {
          {"readOnlyHint", false},
          {"destructiveHint", false},
          {"idempotentHint", true},
          {"openWorldHint", false}
      },
      .handler = [](ToolContext& ctx, const nlohmann::json& args) -> nlohmann::json {
        std::string summary = args.value("summary", "");
        std::vector<std::string> done;
        std::vector<std::string> abandoned;
        if (args.contains("intents_done") && args["intents_done"].is_array()) {
          for (const auto& d : args["intents_done"]) {
            if (d.is_string()) done.push_back(d.get<std::string>());
          }
        }
        if (args.contains("intents_abandoned") && args["intents_abandoned"].is_array()) {
          for (const auto& a : args["intents_abandoned"]) {
            if (a.is_string()) abandoned.push_back(a.get<std::string>());
          }
        }

        ctx.engine.sessions().end(summary, done, abandoned);

        return {
            {"session_id", ctx.session.session_id},
            {"status", "ENDED"},
            {"intents", {
                {"done", done},
                {"abandoned", abandoned}
            }}
        };
      },
      .render = [](const nlohmann::json& structured) -> std::string {
        return "Session " + structured.value("session_id", "") + " ended.";
      }
  });
}

} // namespace gs::mcp
