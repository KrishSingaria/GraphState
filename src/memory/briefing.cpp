#include "memory/briefing.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include "core/clock.hpp"
#include "core/engine.hpp"
#include "db/queries.hpp"
#include "gate/gate.hpp"
#include "memory/memory_store.hpp"
#include "memory/sessions.hpp"
#include "platform/platform.hpp"

namespace gs::memory {

BriefingResult generate_briefing(std::string_view goal, const SessionCtx& session, Engine& engine) {
  std::string intent_id;
  if (!goal.empty()) {
    intent_id = engine.sessions().upsert_intent(goal);
  } else {
    intent_id = session.current_intent_id;
  }

  int64_t now = core::now_ms();
  bool db_online = engine.client().ping();

  // 1. Gather all data
  nlohmann::json user_memories = nlohmann::json::array();
  nlohmann::json project_memories = nlohmann::json::array();
  nlohmann::json open_changeset = nullptr;
  nlohmann::json open_intents = nlohmann::json::array();
  nlohmann::json recent_sessions = nlohmann::json::array();
  nlohmann::json top_pitfalls = nlohmann::json::array();
  nlohmann::json hot_symbols = nlohmann::json::array();
  nlohmann::json critical_symbols = nlohmann::json::array();
  nlohmann::json ungated_changes = nlohmann::json::array();
  nlohmann::json related = nlohmann::json::array();
  nlohmann::json rules_json = nlohmann::json::array();

  int64_t last_indexed_at = 0;

  if (db_online) {
    try {
      // User memories
      user_memories = engine.client().rows(engine.config().user_graph(), db::Query{
          .name = "user_memories_list",
          .cypher = db::Q_MEMORY_LIST,
          .params = {{"kinds", nlohmann::json::array({"preference", "convention", "fact", "todo", "warning"})}, {"limit", 15}},
          .read_only = true
      });

      // Project memories
      project_memories = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "project_memories_list",
          .cypher = db::Q_MEMORY_LIST,
          .params = {{"kinds", nlohmann::json::array({"convention", "warning", "fact", "todo", "preference"})}, {"limit", 15}},
          .read_only = true
      });

      // Open changeset
      auto cs_rows = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "open_changeset",
          .cypher = db::Q_OPEN_CHANGESET,
          .params = nlohmann::json::object(),
          .read_only = true
      });
      if (!cs_rows.empty()) {
        open_changeset = cs_rows[0];
      }

      // Open intents
      open_intents = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "open_intents",
          .cypher = db::Q_OPEN_INTENTS,
          .params = {{"exclude_session", session.session_id}, {"limit", 5}},
          .read_only = true
      });

      // Recent sessions
      recent_sessions = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "recent_sessions",
          .cypher = db::Q_RECENT_SESSIONS,
          .params = {{"exclude", session.session_id}, {"limit", 3}},
          .read_only = true
      });

      // Top pitfalls
      top_pitfalls = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "top_pitfalls",
          .cypher = db::Q_TOP_PITFALLS,
          .params = {{"limit", 7}},
          .read_only = true
      });

      // Hot symbols (last 7 days)
      hot_symbols = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "hot_symbols",
          .cypher = db::Q_HOT_SYMBOLS,
          .params = {{"since", now - 7 * 86'400'000LL}, {"limit", 10}},
          .read_only = true
      });

      // Critical symbols (PageRank high tier)
      critical_symbols = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "critical_symbols",
          .cypher = db::Q_CRITICAL_SYMBOLS,
          .params = {{"limit", 5}},
          .read_only = true
      });

      // Ungated changes
      int64_t last_ended_session_ts = 0;
      if (!recent_sessions.empty() && recent_sessions[0].contains("ended_at")) {
        last_ended_session_ts = recent_sessions[0].value("ended_at", 0LL);
      }
      ungated_changes = engine.client().rows(engine.config().project_graph(), db::Query{
          .name = "ungated_since",
          .cypher = db::Q_UNGATED_SINCE,
          .params = {{"since", last_ended_session_ts}},
          .read_only = true
      });

      // Meta
      auto meta_row = engine.client().one(engine.config().project_graph(), db::Query{
          .name = "meta_get",
          .cypher = db::Q_META_GET,
          .params = nlohmann::json::object(),
          .read_only = true
      });
      if (!meta_row.is_null()) {
        last_indexed_at = meta_row.value("last_indexed_at", 0LL);
      }
    } catch (...) {}
  }

  // Related memories via recall if goal provided
  if (!goal.empty()) {
    try {
      RecallRequest rreq;
      rreq.query = std::string(goal);
      rreq.limit = 8;
      auto rres = engine.memory_store().recall(rreq, session);
      for (const auto& item : rres.results) {
        related.push_back({
            {"type", item.type},
            {"id", item.id},
            {"text", item.text},
            {"score", item.score}
        });
      }
    } catch (...) {}
  }

  // Quality rules
  const auto& rules_map = engine.gate().rules_engine().rules();
  for (const auto& [rid, rcfg] : rules_map) {
    if (rcfg.severity == "error") {
      rules_json.push_back({{"id", rid}, {"severity", "error"}});
    }
  }

  // Workspace stats from FactStore
  size_t total_files = engine.fact_store().file_count();
  size_t total_symbols = engine.fact_store().total_symbols();
  std::map<std::string, int> lang_counts;
  size_t function_count = 0;
  size_t class_count = 0;

  for (const auto& f : engine.fact_store().files()) {
    const auto* facts = engine.fact_store().get(f);
    if (!facts) continue;
    std::string l_name = std::string(lang::LanguageRegistry::get().info(facts->lang).name);
    if (!l_name.empty() && l_name != "unknown") {
      lang_counts[l_name]++;
    }
    for (const auto& s : facts->symbols) {
      if (s.kind == lang::SymbolKind::Function || s.kind == lang::SymbolKind::Method) {
        ++function_count;
      } else if (s.kind == lang::SymbolKind::Class) {
        ++class_count;
      }
    }
  }

  nlohmann::json workspace_info = {
      {"root", platform::to_utf8(engine.config().root)},
      {"graph", engine.config().project_graph()},
      {"files", total_files},
      {"symbols", total_symbols},
      {"functions", function_count},
      {"classes", class_count},
      {"languages", lang_counts},
      {"last_indexed_at", last_indexed_at}
  };

  // 2. Assemble Markdown
  std::string ws_name = platform::to_utf8(engine.config().root.filename());
  if (ws_name.empty()) ws_name = "workspace";

  std::ostringstream md;
  md << "# GraphState briefing — " << ws_name << " (session " << session.session_id << ")\n";

  // You (user preferences)
  if (!user_memories.empty()) {
    md << "## You (user preferences)\n";
    for (const auto& m : user_memories) {
      std::string id = m.value("id", "");
      std::string content = m.value("content", "");
      md << "- [preference] " << content << " (" << id << ")\n";
    }
  }

  // This project
  if (!project_memories.empty()) {
    md << "## This project\n";
    for (const auto& m : project_memories) {
      std::string id = m.value("id", "");
      std::string kind = m.value("kind", "convention");
      std::string content = m.value("content", "");
      md << "- [" << kind << "] " << content << " (" << id << ")\n";
    }
  }

  // Open changeset
  if (!open_changeset.is_null() && open_changeset.is_object()) {
    std::string cs_id = open_changeset.value("id", "");
    std::string cs_intent = open_changeset.value("intent", "");
    size_t f_count = open_changeset.contains("files") && open_changeset["files"].is_array() ? open_changeset["files"].size() : 0;
    int obligations = open_changeset.value("open_obligations", 0);
    md << "## Open changeset (continue it)\n";
    md << "- " << cs_id << " \"" << cs_intent << "\" — " << f_count << " files, " << obligations << " obligations\n";
  }

  // Unfinished work
  if (!open_intents.empty()) {
    md << "## Unfinished work\n";
    for (const auto& oi : open_intents) {
      std::string goal_str = oi.value("goal", "");
      std::string s_status = oi.value("session_status", "OPEN");
      md << "- \"" << goal_str << "\" — previous session " << s_status << "\n";
    }
  }

  // Known pitfalls
  if (!top_pitfalls.empty()) {
    md << "## Known pitfalls (most frequent first)\n";
    for (const auto& p : top_pitfalls) {
      int count = p.value("count", 1);
      std::string code = p.value("code", "");
      std::string sym = p.value("symbol_id", "");
      std::string file = p.value("file", "");
      std::string lesson = p.value("lesson", "");
      std::string target = sym.empty() ? file : sym;
      md << "- ×" << count << " " << code << " " << target << " — " << lesson << "\n";
    }
  }

  // Recent sessions
  if (!recent_sessions.empty()) {
    md << "## Recent sessions\n";
    for (const auto& s : recent_sessions) {
      std::string client = s.value("client", "agent");
      std::string st = s.value("status", "ENDED");
      std::string summary = s.value("summary", "");
      int applied = s.value("applied", 0);
      int rejected = s.value("rejected", 0);
      md << "- " << client << " " << st << ": " << summary << " (" << applied << " applied, " << rejected << " rejected)\n";
    }
  }

  // Recently changed hot spots
  if (!hot_symbols.empty()) {
    md << "## Recently changed hot spots\n";
    for (const auto& hs : hot_symbols) {
      std::string id = hs.value("id", "");
      int changes = hs.value("changes", 1);
      md << "- " << id << " (" << changes << " changes)\n";
    }
  }

  // Changes made outside the gate since your last session
  if (!ungated_changes.empty()) {
    md << "## Changes made outside the gate since your last session\n";
    for (const auto& uc : ungated_changes) {
      if (uc.contains("files") && uc["files"].is_array()) {
        for (const auto& f : uc["files"]) {
          md << "- " << f.get<std::string>() << " (UNGATED)\n";
        }
      }
    }
  }

  // Enforced rules
  if (!rules_json.empty()) {
    md << "## Enforced rules\n";
    md << "- ";
    for (size_t i = 0; i < rules_json.size(); ++i) {
      if (i > 0) md << ", ";
      md << rules_json[i].value("id", "");
    }
    md << "\n";
  }

  // Workspace
  md << "## Workspace\n";
  md << "- " << total_files << " files";
  if (!lang_counts.empty()) {
    md << " (";
    size_t i = 0;
    for (const auto& [lang_name, cnt] : lang_counts) {
      if (i > 0) md << ", ";
      md << lang_name << " " << cnt;
      ++i;
    }
    md << ")";
  }
  md << " · " << function_count << " functions · " << class_count << " classes\n";

  std::string markdown_text = md.str();

  // Enforce 10k characters budget
  if (markdown_text.size() > 10'000) {
    markdown_text = markdown_text.substr(0, 9'990) + "\n...";
  }

  // 3. Assemble structuredContent JSON
  nlohmann::json structured = {
      {"session_id", session.session_id},
      {"intent_id", intent_id.empty() ? nullptr : nlohmann::json(intent_id)},
      {"workspace", workspace_info},
      {"user_memories", user_memories},
      {"project_memories", project_memories},
      {"open_intents", open_intents},
      {"recent_sessions", recent_sessions},
      {"top_pitfalls", top_pitfalls},
      {"hot_symbols", hot_symbols},
      {"critical_symbols", critical_symbols},
      {"ungated_changes", ungated_changes},
      {"open_changeset", open_changeset},
      {"related", related},
      {"rules", rules_json}
  };

  return BriefingResult{
      .markdown = std::move(markdown_text),
      .structured_content = std::move(structured)
  };
}

} // namespace gs::memory
