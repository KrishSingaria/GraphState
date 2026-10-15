#pragma once

#include <array>
#include <string_view>

namespace gs::db {

// ─────────────────────────────────────────────────────────────────────────────
// Meta & Schema
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_META_UPSERT =
    "MERGE (m:Meta {id:'meta'})\n"
    "ON CREATE SET m.schema_version = $schema_version, m.created_at = $now\n"
    "SET m.root = $root, m.workspace_key = $key, m.engine_version = $engine_version";

constexpr std::string_view Q_META_GET =
    "MATCH (m:Meta {id:'meta'}) RETURN toJSON({schema_version: m.schema_version, last_indexed_at: m.last_indexed_at}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Code Subgraph Writes
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_FILES_UPSERT =
    "UNWIND $rows AS r\n"
    "MERGE (f:File {id: r.id})\n"
    "SET f.language = r.language, f.semantic = r.semantic, f.hash = r.hash, f.size = r.size,\n"
    "    f.mtime_ns = r.mtime_ns, f.line_count = r.line_count, f.parse_ok = r.parse_ok,\n"
    "    f.symbol_count = r.symbol_count, f.skipped_reason = r.skipped_reason, f.indexed_at = $now";

constexpr std::string_view Q_FUNCTIONS_DELETE_STALE =
    "UNWIND $files AS fid\n"
    "MATCH (s:Function {file: fid})\n"
    "WHERE NOT s.id IN $keep\n"
    "DELETE s";

constexpr std::string_view Q_CLASSES_DELETE_STALE =
    "UNWIND $files AS fid\n"
    "MATCH (s:Class {file: fid})\n"
    "WHERE NOT s.id IN $keep\n"
    "DELETE s";

constexpr std::string_view Q_CLASSES_UPSERT =
    "UNWIND $rows AS r\n"
    "MERGE (c:Class {id: r.id})\n"
    "SET c.name = r.name, c.qualname = r.qualname, c.file = r.file, c.kind = r.kind, c.bases = r.bases,\n"
    "    c.is_exported = r.is_exported, c.start_line = r.start_line, c.end_line = r.end_line,\n"
    "    c.start_byte = r.start_byte, c.end_byte = r.end_byte, c.doc = r.doc, c.search_text = r.search_text";

constexpr std::string_view Q_FUNCTIONS_UPSERT =
    "UNWIND $rows AS r\n"
    "MERGE (f:Function {id: r.id})\n"
    "SET f.name = r.name, f.qualname = r.qualname, f.file = r.file, f.kind = r.kind, f.class_id = r.class_id,\n"
    "    f.signature = r.signature, f.params_json = r.params_json, f.required_params = r.required_params,\n"
    "    f.max_params = r.max_params, f.is_async = r.is_async, f.is_exported = r.is_exported, f.is_static = r.is_static,\n"
    "    f.decorators = r.decorators, f.return_type = r.return_type, f.start_line = r.start_line, f.end_line = r.end_line,\n"
    "    f.start_byte = r.start_byte, f.end_byte = r.end_byte, f.body_hash = r.body_hash, f.loc = r.loc,\n"
    "    f.complexity = r.complexity, f.max_nesting = r.max_nesting, f.doc = r.doc, f.search_text = r.search_text";

constexpr std::string_view Q_STRUCT_EDGES_CLEAR =
    "UNWIND $files AS fid\n"
    "MATCH (f:File {id: fid})-[r:DEFINES|IMPORTS]->()\n"
    "DELETE r";

constexpr std::string_view Q_CLASS_EDGES_CLEAR =
    "UNWIND $files AS fid\n"
    "MATCH (c:Class {file: fid})-[r:HAS_METHOD|INHERITS]->()\n"
    "DELETE r";

constexpr std::string_view Q_DEFINES_CREATE =
    "UNWIND $rows AS r\n"
    "MATCH (f:File {id: r.file})\n"
    "OPTIONAL MATCH (fn:Function {id: r.sym})\n"
    "OPTIONAL MATCH (c:Class {id: r.sym})\n"
    "WITH f, coalesce(fn, c) AS s\n"
    "WHERE s IS NOT NULL\n"
    "CREATE (f)-[:DEFINES]->(s)";

constexpr std::string_view Q_HAS_METHOD_CREATE =
    "UNWIND $rows AS r\n"
    "MATCH (c:Class {id: r.cls}), (m:Function {id: r.fn})\n"
    "CREATE (c)-[:HAS_METHOD]->(m)";

constexpr std::string_view Q_INHERITS_CREATE =
    "UNWIND $rows AS r\n"
    "MATCH (c:Class {id: r.cls}), (b:Class {id: r.base})\n"
    "CREATE (c)-[:INHERITS {confidence: r.confidence}]->(b)";

constexpr std::string_view Q_IMPORTS_CREATE =
    "UNWIND $rows AS r\n"
    "MATCH (a:File {id: r.src}), (b:File {id: r.dst})\n"
    "CREATE (a)-[:IMPORTS {names: r.names, line: r.line, whole_module: r.whole_module, alias: r.alias}]->(b)";

constexpr std::string_view Q_CALLS_CLEAR_FROM_FILES =
    "UNWIND $files AS fid\n"
    "MATCH (s:Function {file: fid})-[r:CALLS]->()\n"
    "DELETE r";

constexpr std::string_view Q_CALLS_CREATE =
    "UNWIND $rows AS r\n"
    "MATCH (a:Function {id: r.caller}), (b:Function {id: r.callee})\n"
    "CREATE (a)-[:CALLS {line: r.line, col: r.col, pos_args: r.pos_args, kw_names: r.kw_names, has_star: r.has_star,\n"
    "                    has_dstar: r.has_dstar, confidence: r.confidence, call_kind: r.call_kind, call_text: r.call_text}]->(b)";

constexpr std::string_view Q_FILES_DELETE =
    "UNWIND $files AS fid\n"
    "MATCH (f:File {id: fid})\n"
    "DELETE f";

constexpr std::string_view Q_CODE_WIPE =
    "MATCH (n) WHERE n:File OR n:Class OR n:Function DELETE n";

constexpr std::string_view Q_META_TOUCH_INDEXED =
    "MATCH (m:Meta {id:'meta'}) SET m.last_indexed_at = $now";

// ─────────────────────────────────────────────────────────────────────────────
// Code Subgraph Reads (RO)
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_FILE_HASHES =
    "MATCH (f:File) RETURN toJSON(collect({id: f.id, hash: f.hash})) AS j";

constexpr std::string_view Q_READ_SYMBOL_CONTEXT =
    "MATCH (s:Function {id: $id})\n"
    "RETURN toJSON({\n"
    "  callers:      [(c:Function)-[e:CALLS]->(s) | {id: c.id, file: c.file, line: e.line, col: e.col, call: e.call_text, confidence: e.confidence}][0..$limit],\n"
    "  caller_count: size([(c:Function)-[e:CALLS]->(s) | 1]),\n"
    "  callees:      [(s)-[e:CALLS]->(t:Function) | {id: t.id, file: t.file, signature: t.signature, line: e.line}][0..$limit],\n"
    "  history:      [(d:Decision)-[:MUTATED]->(s) | {patch_id: d.patch_id, status: d.status, intent: d.intent, reasoning: d.reasoning, ts: d.ts}],\n"
    "  pitfalls:     [(p:Pitfall)-[:ABOUT]->(s) | {key: p.key, code: p.code, lesson: p.lesson, count: p.count, last_seen: p.last_seen}],\n"
    "  memories:     [(m:Memory)-[:ABOUT]->(s) WHERE m.archived = false | {id: m.id, kind: m.kind, content: m.content}]\n"
    "}) AS j";

constexpr std::string_view Q_READ_CLASS_CONTEXT =
    "MATCH (c:Class {id: $id})\n"
    "RETURN toJSON({\n"
    "  methods:   [(c)-[:HAS_METHOD]->(m:Function) | {id: m.id, signature: m.signature, start_line: m.start_line}],\n"
    "  bases:     [(c)-[:INHERITS]->(b:Class) | b.id],\n"
    "  subclasses:[(s:Class)-[:INHERITS]->(c) | s.id],\n"
    "  instantiated_by: [(x:Function)-[e:CALLS {call_kind:'constructor'}]->(:Function {class_id: $id}) | {id: x.id, file: x.file, line: e.line}][0..$limit],\n"
    "  history:   [(d:Decision)-[:MUTATED]->(c) | {patch_id: d.patch_id, status: d.status, intent: d.intent, ts: d.ts}],\n"
    "  pitfalls:  [(p:Pitfall)-[:ABOUT]->(c) | {key: p.key, code: p.code, lesson: p.lesson, count: p.count}]\n"
    "}) AS j";

constexpr std::string_view Q_CALLERS_OF_IDS =
    "UNWIND $ids AS tid\n"
    "MATCH (c:Function)-[e:CALLS]->(t:Function {id: tid})\n"
    "WHERE NOT c.file IN $exclude_files\n"
    "RETURN toJSON({target: tid, caller: c.id, file: c.file, line: e.line, col: e.col, call: e.call_text,\n"
    "               pos_args: e.pos_args, kw_names: e.kw_names, has_star: e.has_star, has_dstar: e.has_dstar,\n"
    "               confidence: e.confidence, call_kind: e.call_kind}) AS j";

constexpr std::string_view Q_IMPORTERS_OF_NAMES =
    "MATCH (a:File)-[i:IMPORTS]->(b:File {id: $file})\n"
    "WHERE NOT a.id IN $exclude_files AND any(n IN i.names WHERE n IN $names)\n"
    "RETURN toJSON({file: a.id, line: i.line, names: [n IN i.names WHERE n IN $names]}) AS j";

constexpr std::string_view Q_IMPORTERS_OF_FILE =
    "MATCH (a:File)-[i:IMPORTS]->(b:File {id: $file})\n"
    "WHERE NOT a.id IN $exclude_files\n"
    "RETURN toJSON({file: a.id, line: i.line, names: i.names, whole_module: i.whole_module}) AS j";

constexpr std::string_view Q_IMPACT_D1 =
    "MATCH p = (c:Function)-[:CALLS*1..1]->(t:Function {id: $id})\n"
    "WITH c, min(length(p)) AS depth\n"
    "ORDER BY depth, c.id\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: c.id, file: c.file, signature: c.signature, depth: depth, criticality: c.criticality}) AS j";

constexpr std::string_view Q_IMPACT_D2 =
    "MATCH p = (c:Function)-[:CALLS*1..2]->(t:Function {id: $id})\n"
    "WITH c, min(length(p)) AS depth\n"
    "ORDER BY depth, c.id\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: c.id, file: c.file, signature: c.signature, depth: depth, criticality: c.criticality}) AS j";

constexpr std::string_view Q_IMPACT_D3 =
    "MATCH p = (c:Function)-[:CALLS*1..3]->(t:Function {id: $id})\n"
    "WITH c, min(length(p)) AS depth\n"
    "ORDER BY depth, c.id\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: c.id, file: c.file, signature: c.signature, depth: depth, criticality: c.criticality}) AS j";

constexpr std::string_view Q_IMPACT_D4 =
    "MATCH p = (c:Function)-[:CALLS*1..4]->(t:Function {id: $id})\n"
    "WITH c, min(length(p)) AS depth\n"
    "ORDER BY depth, c.id\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: c.id, file: c.file, signature: c.signature, depth: depth, criticality: c.criticality}) AS j";

constexpr std::string_view Q_IMPACT_D5 =
    "MATCH p = (c:Function)-[:CALLS*1..5]->(t:Function {id: $id})\n"
    "WITH c, min(length(p)) AS depth\n"
    "ORDER BY depth, c.id\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: c.id, file: c.file, signature: c.signature, depth: depth, criticality: c.criticality}) AS j";

constexpr std::array<std::string_view, 5> Q_IMPACT_BY_DEPTH = {
    Q_IMPACT_D1, Q_IMPACT_D2, Q_IMPACT_D3, Q_IMPACT_D4, Q_IMPACT_D5
};

inline constexpr std::string_view Q_IMPACT(int depth) {
  if (depth < 1) depth = 1;
  if (depth > 5) depth = 5;
  return Q_IMPACT_BY_DEPTH[static_cast<size_t>(depth - 1)];
}

constexpr std::string_view Q_IMPACT_FILES_D1 =
    "MATCH (c:Function)-[:CALLS*1..1]->(t:Function {id: $id})\n"
    "RETURN toJSON(collect(DISTINCT c.file)) AS j";

constexpr std::string_view Q_IMPACT_FILES_D2 =
    "MATCH (c:Function)-[:CALLS*1..2]->(t:Function {id: $id})\n"
    "RETURN toJSON(collect(DISTINCT c.file)) AS j";

constexpr std::string_view Q_IMPACT_FILES_D3 =
    "MATCH (c:Function)-[:CALLS*1..3]->(t:Function {id: $id})\n"
    "RETURN toJSON(collect(DISTINCT c.file)) AS j";

constexpr std::string_view Q_IMPACT_FILES_D4 =
    "MATCH (c:Function)-[:CALLS*1..4]->(t:Function {id: $id})\n"
    "RETURN toJSON(collect(DISTINCT c.file)) AS j";

constexpr std::string_view Q_IMPACT_FILES_D5 =
    "MATCH (c:Function)-[:CALLS*1..5]->(t:Function {id: $id})\n"
    "RETURN toJSON(collect(DISTINCT c.file)) AS j";

constexpr std::array<std::string_view, 5> Q_IMPACT_FILES_BY_DEPTH = {
    Q_IMPACT_FILES_D1, Q_IMPACT_FILES_D2, Q_IMPACT_FILES_D3, Q_IMPACT_FILES_D4, Q_IMPACT_FILES_D5
};

inline constexpr std::string_view Q_IMPACT_FILES(int depth) {
  if (depth < 1) depth = 1;
  if (depth > 5) depth = 5;
  return Q_IMPACT_FILES_BY_DEPTH[static_cast<size_t>(depth - 1)];
}

constexpr std::string_view Q_FILE_OUTLINE =
    "MATCH (f:File {id: $file})\n"
    "RETURN toJSON({\n"
    "  file: f.id, language: f.language, line_count: f.line_count, parse_ok: f.parse_ok, hash: f.hash,\n"
    "  imports: [(f)-[i:IMPORTS]->(t:File) | {file: t.id, names: i.names, line: i.line}],\n"
    "  imported_by: [(o:File)-[:IMPORTS]->(f) | o.id],\n"
    "  classes: [(f)-[:DEFINES]->(c:Class) | {id: c.id, name: c.name, kind: c.kind, bases: c.bases, start_line: c.start_line, end_line: c.end_line,\n"
    "             methods: [(c)-[:HAS_METHOD]->(m:Function) | {name: m.name, signature: m.signature, start_line: m.start_line, end_line: m.end_line}]}],\n"
    "  functions: [(f)-[:DEFINES]->(fn:Function) WHERE fn.kind <> 'module' | {id: fn.id, name: fn.name, signature: fn.signature,\n"
    "             start_line: fn.start_line, end_line: fn.end_line, fan_in: size([(x:Function)-[:CALLS]->(fn) | 1])}]\n"
    "}) AS j";

constexpr std::string_view Q_MODULE_MAP =
    "MATCH (f:File)\n"
    "WHERE f.id STARTS WITH $prefix\n"
    "WITH f\n"
    "ORDER BY f.id\n"
    "LIMIT $limit\n"
    "RETURN toJSON({file: f.id, language: f.language, symbols: f.symbol_count,\n"
    "               imports: [(f)-[:IMPORTS]->(t:File) | t.id],\n"
    "               imported_by_count: size([(o:File)-[:IMPORTS]->(f) | 1])}) AS j";

constexpr std::string_view Q_HOTSPOTS =
    "MATCH (c:Function)-[:CALLS]->(t:Function)\n"
    "WHERE t.file STARTS WITH $prefix\n"
    "WITH t, count(c) AS fan_in\n"
    "ORDER BY fan_in DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: t.id, signature: t.signature, fan_in: fan_in}) AS j";

constexpr std::string_view Q_SEARCH_FUNCTIONS =
    "CALL db.idx.fulltext.queryNodes('Function', $q) YIELD node, score\n"
    "WITH node, score WHERE node.kind <> 'module'\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: node.id, kind: node.kind, signature: node.signature, file: node.file,\n"
    "               start_line: node.start_line, doc: node.doc, score: score}) AS j";

constexpr std::string_view Q_SEARCH_CLASSES =
    "CALL db.idx.fulltext.queryNodes('Class', $q) YIELD node, score\n"
    "WITH node, score\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: node.id, kind: node.kind, signature: node.name, file: node.file,\n"
    "               start_line: node.start_line, doc: node.doc, score: score}) AS j";

constexpr std::string_view Q_COUNTS =
    "MATCH (n)\n"
    "WITH labels(n)[0] AS label, count(n) AS n\n"
    "RETURN toJSON({label: label, n: n}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Sessions
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_SESSION_CREATE =
    "CREATE (:Session {id: $id, client: $client, client_version: $client_version, status: 'ACTIVE',\n"
    "                  started_at: $now, last_seen_at: $now, ended_at: 0, summary: '',\n"
    "                  patch_count: 0, applied_count: 0, rejected_count: 0})";

constexpr std::string_view Q_SESSION_HEARTBEAT =
    "MATCH (s:Session {id: $id}) SET s.last_seen_at = $now";

constexpr std::string_view Q_SESSIONS_MARK_INTERRUPTED =
    "MATCH (s:Session {status: 'ACTIVE'})\n"
    "WHERE s.last_seen_at < $cutoff\n"
    "SET s.status = 'INTERRUPTED', s.ended_at = s.last_seen_at\n"
    "RETURN toJSON(collect(s.id)) AS j";

constexpr std::string_view Q_SESSION_END =
    "MATCH (s:Session {id: $id})\n"
    "SET s.status = 'ENDED', s.summary = $summary, s.ended_at = $now";

constexpr std::string_view Q_SESSION_AUTO_SUMMARY_DATA =
    "MATCH (s:Session {id: $id})\n"
    "RETURN toJSON({\n"
    "  intents: [(s)-[:HAS_INTENT]->(i:Intent) | i.goal],\n"
    "  decisions: [(s)-[:MADE]->(d:Decision) | {status: d.status, files: d.files}],\n"
    "  patch_count: s.patch_count, applied: s.applied_count, rejected: s.rejected_count\n"
    "}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Intents
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_INTENT_FIND =
    "MATCH (:Session {id: $session})-[:HAS_INTENT]->(i:Intent {goal_norm: $goal_norm}) RETURN toJSON({id: i.id}) AS j";

constexpr std::string_view Q_INTENT_CREATE =
    "MATCH (s:Session {id: $session})\n"
    "CREATE (s)-[:HAS_INTENT]->(:Intent {id: $id, goal: $goal, goal_norm: $goal_norm, status: 'OPEN', created_at: $now, updated_at: $now})";

constexpr std::string_view Q_INTENT_TOUCH =
    "MATCH (i:Intent {id: $id}) SET i.updated_at = $now";

constexpr std::string_view Q_INTENTS_SET_STATUS =
    "UNWIND $ids AS iid\n"
    "MATCH (i:Intent {id: iid}) SET i.status = $status, i.updated_at = $now";

// ─────────────────────────────────────────────────────────────────────────────
// Decisions
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_DECISION_RECORD =
    "MERGE (d:Decision {patch_id: $d.patch_id})\n"
    "ON CREATE SET d.status = $d.status, d.actor = $d.actor, d.intent = $d.intent, d.reasoning = $d.reasoning,\n"
    "              d.failed_phase = $d.failed_phase, d.violation_codes = $d.violation_codes, d.files = $d.files,\n"
    "              d.symbols = $d.symbols, d.ts = $d.ts, d.duration_ms = $d.duration_ms\n"
    "WITH d\n"
    "OPTIONAL MATCH (s:Session {id: $session})\n"
    "FOREACH (_ IN CASE WHEN s IS NULL THEN [] ELSE [1] END |\n"
    "  MERGE (s)-[:MADE]->(d)\n"
    "  SET s.patch_count = s.patch_count + 1,\n"
    "      s.applied_count = s.applied_count + CASE WHEN $d.status = 'APPLIED' THEN 1 ELSE 0 END,\n"
    "      s.rejected_count  = s.rejected_count  + CASE WHEN $d.status = 'REJECTED'  THEN 1 ELSE 0 END)\n"
    "WITH d\n"
    "OPTIONAL MATCH (i:Intent {id: $intent_id})\n"
    "FOREACH (_ IN CASE WHEN i IS NULL THEN [] ELSE [1] END | MERGE (i)-[:RESOLVED_BY]->(d))\n"
    "WITH d\n"
    "UNWIND $d.files AS fid\n"
    "MATCH (f:File {id: fid})\n"
    "MERGE (d)-[:TOUCHED]->(f)";

constexpr std::string_view Q_DECISION_EXISTS =
    "MATCH (d:Decision {patch_id: $patch_id}) RETURN toJSON(true) AS j";

constexpr std::string_view Q_DECISION_LINK_MUTATED =
    "MATCH (d:Decision {patch_id: $patch_id})\n"
    "UNWIND $rows AS r\n"
    "OPTIONAL MATCH (fn:Function {id: r.id})\n"
    "OPTIONAL MATCH (c:Class {id: r.id})\n"
    "WITH d, r, coalesce(fn, c) AS s\n"
    "WHERE s IS NOT NULL\n"
    "MERGE (d)-[m:MUTATED]->(s)\n"
    "SET m.change = r.change";

// ─────────────────────────────────────────────────────────────────────────────
// Pitfalls
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_PITFALLS_UPSERT =
    "MATCH (d:Decision {patch_id: $patch_id})\n"
    "UNWIND $rows AS r\n"
    "MERGE (p:Pitfall {key: r.key})\n"
    "ON CREATE SET p.code = r.code, p.rule_id = r.rule_id, p.symbol_id = r.symbol_id, p.file = r.file,\n"
    "              p.lesson = r.lesson, p.count = 0, p.resolved_count = 0, p.first_seen = $now, p.last_resolved = 0\n"
    "SET p.count = p.count + 1, p.last_seen = $now, p.message = r.message,\n"
    "    p.lesson = CASE WHEN size(r.lesson) > size(p.lesson) THEN r.lesson ELSE p.lesson END\n"
    "MERGE (d)-[:TRIGGERED]->(p)\n"
    "WITH p, r\n"
    "OPTIONAL MATCH (fn:Function {id: r.symbol_id})\n"
    "OPTIONAL MATCH (c:Class {id: r.symbol_id})\n"
    "OPTIONAL MATCH (f:File {id: r.file})\n"
    "WITH p, coalesce(fn, c) AS s, f\n"
    "FOREACH (_ IN CASE WHEN s IS NULL THEN [] ELSE [1] END | MERGE (p)-[:ABOUT]->(s))\n"
    "FOREACH (_ IN CASE WHEN f IS NULL THEN [] ELSE [1] END | MERGE (p)-[:IN_FILE]->(f))";

constexpr std::string_view Q_PITFALLS_RESOLVE =
    "UNWIND $symbol_ids AS sid\n"
    "MATCH (p:Pitfall {symbol_id: sid})\n"
    "SET p.resolved_count = p.resolved_count + 1, p.last_resolved = $now";

constexpr std::string_view Q_LESSONS_FOR =
    "MATCH (p:Pitfall)\n"
    "WHERE p.symbol_id IN $symbol_ids OR p.file IN $files\n"
    "WITH p\n"
    "ORDER BY p.count DESC, p.last_seen DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({key: p.key, code: p.code, rule_id: p.rule_id, symbol_id: p.symbol_id, file: p.file,\n"
    "               lesson: p.lesson, count: p.count, last_seen: p.last_seen, resolved_count: p.resolved_count}) AS j";

constexpr std::string_view Q_PITFALLS_LIST =
    "MATCH (p:Pitfall)\n"
    "WHERE ($symbol = '' OR p.symbol_id = $symbol) AND ($file = '' OR p.file = $file) AND ($code = '' OR p.code = $code)\n"
    "WITH p\n"
    "ORDER BY p.count DESC, p.last_seen DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({key: p.key, code: p.code, rule_id: p.rule_id, symbol_id: p.symbol_id, file: p.file, lesson: p.lesson,\n"
    "               count: p.count, resolved_count: p.resolved_count, first_seen: p.first_seen, last_seen: p.last_seen,\n"
    "               last_resolved: p.last_resolved}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Memories
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_MEMORY_FIND_DUP =
    "MATCH (m:Memory {kind: $kind, content_norm: $content_norm}) WHERE m.archived = false\n"
    "RETURN toJSON({id: m.id}) AS j";

constexpr std::string_view Q_MEMORY_CREATE =
    "CREATE (n:Memory {id: $m.id, kind: $m.kind, scope: $m.scope, content: $m.content, content_norm: $m.content_norm,\n"
    "                  tags: $m.tags, anchors: $m.anchors, importance: $m.importance, archived: false, superseded_by: '',\n"
    "                  source_session: $m.source_session, created_at: $now, updated_at: $now})";

constexpr std::string_view Q_MEMORY_TOUCH =
    "MATCH (m:Memory {id: $id}) SET m.updated_at = $now, m.importance = $importance, m.tags = $tags";

constexpr std::string_view Q_MEMORY_SUPERSEDE =
    "MATCH (m:Memory {id: $old_id}) SET m.archived = true, m.superseded_by = $new_id, m.updated_at = $now";

constexpr std::string_view Q_MEMORY_LINK =
    "MATCH (m:Memory {id: $id})\n"
    "OPTIONAL MATCH (s:Session {id: $session})\n"
    "FOREACH (_ IN CASE WHEN s IS NULL THEN [] ELSE [1] END | MERGE (s)-[:NOTED]->(m))\n"
    "WITH m\n"
    "UNWIND $anchors AS aid\n"
    "OPTIONAL MATCH (fn:Function {id: aid})\n"
    "OPTIONAL MATCH (c:Class {id: aid})\n"
    "OPTIONAL MATCH (f:File {id: aid})\n"
    "WITH m, coalesce(fn, c, f) AS t\n"
    "WHERE t IS NOT NULL\n"
    "MERGE (m)-[:ABOUT]->(t)";

constexpr std::string_view Q_MEMORY_LIST =
    "MATCH (m:Memory)\n"
    "WHERE m.archived = false AND m.kind IN $kinds\n"
    "WITH m\n"
    "ORDER BY m.importance DESC, m.updated_at DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: m.id, kind: m.kind, scope: m.scope, content: m.content, tags: m.tags, anchors: m.anchors,\n"
    "               importance: m.importance, updated_at: m.updated_at}) AS j";

constexpr std::string_view Q_MEMORY_DELETE =
    "MATCH (m:Memory {id: $id}) DELETE m";

// ─────────────────────────────────────────────────────────────────────────────
// Changesets
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_CHANGESET_CREATE =
    "MATCH (s:Session {id: $session})\n"
    "CREATE (s)-[:WORKED_ON]->(:Changeset {id: $id, intent: $intent, status: 'OPEN', files: [], open_obligations: 0,\n"
    "                                      created_at: $now, updated_at: $now, closed_at: 0})";

constexpr std::string_view Q_CHANGESET_TOUCH =
    "MATCH (c:Changeset {id: $id})\n"
    "SET c.files = $files, c.open_obligations = $open_obligations, c.updated_at = $now\n"
    "WITH c\n"
    "MATCH (s:Session {id: $session})\n"
    "MERGE (s)-[:WORKED_ON]->(c)";

constexpr std::string_view Q_CHANGESET_SET_STATUS =
    "MATCH (c:Changeset {id: $id}) SET c.status = $status, c.updated_at = $now, c.closed_at = $now";

constexpr std::string_view Q_CHANGESET_LINK_DECISION =
    "MATCH (c:Changeset {id: $id}), (d:Decision {patch_id: $patch_id})\n"
    "MERGE (c)-[:INCLUDES]->(d)";

constexpr std::string_view Q_OBLIGATIONS_CLEAR =
    "MATCH (:Changeset {id: $id})-[:HAS_OBLIGATION]->(o:Obligation) DELETE o";

constexpr std::string_view Q_OBLIGATIONS_CREATE =
    "MATCH (c:Changeset {id: $id})\n"
    "UNWIND $rows AS r\n"
    "CREATE (c)-[:HAS_OBLIGATION]->(:Obligation {id: r.oid, code: r.code, message: r.message, fix_hint: r.fix_hint,\n"
    "                                            file: r.file, line: r.line, col: r.col, symbol_id: r.symbol_id, created_at: $now})";

constexpr std::string_view Q_OPEN_CHANGESET =
    "MATCH (c:Changeset {status: 'OPEN'})\n"
    "RETURN toJSON({id: c.id, intent: c.intent, files: c.files, open_obligations: c.open_obligations,\n"
    "               created_at: c.created_at, updated_at: c.updated_at,\n"
    "               sessions: [(s:Session)-[:WORKED_ON]->(c) | s.id],\n"
    "               obligations: [(c)-[:HAS_OBLIGATION]->(o:Obligation) | {id: o.id, code: o.code, message: o.message, file: o.file,\n"
    "                             line: o.line, col: o.col, symbol_id: o.symbol_id, fix_hint: o.fix_hint}]}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Relinks
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_RELINK_PITFALLS =
    "UNWIND $ids AS sid\n"
    "MATCH (p:Pitfall {symbol_id: sid})\n"
    "OPTIONAL MATCH (fn:Function {id: sid})\n"
    "OPTIONAL MATCH (c:Class {id: sid})\n"
    "WITH p, coalesce(fn, c) AS s\n"
    "WHERE s IS NOT NULL\n"
    "MERGE (p)-[:ABOUT]->(s)";

constexpr std::string_view Q_RELINK_MEMORIES =
    "UNWIND $ids AS sid\n"
    "MATCH (m:Memory)\n"
    "WHERE sid IN m.anchors\n"
    "OPTIONAL MATCH (fn:Function {id: sid})\n"
    "OPTIONAL MATCH (c:Class {id: sid})\n"
    "OPTIONAL MATCH (f:File {id: sid})\n"
    "WITH m, coalesce(fn, c, f) AS t\n"
    "WHERE t IS NOT NULL\n"
    "MERGE (m)-[:ABOUT]->(t)";

// ─────────────────────────────────────────────────────────────────────────────
// Graph Analytics (FalkorDB algorithms)
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_ALGO_PAGERANK_FUNCTIONS =
    "CALL algo.pageRank('Function', 'CALLS') YIELD node, score\n"
    "RETURN toJSON({id: node.id, score: score}) AS j";

constexpr std::string_view Q_SET_CRITICALITY =
    "UNWIND $rows AS r\n"
    "MATCH (f:Function {id: r.id})\n"
    "SET f.pagerank = r.pagerank, f.pr_pct = r.pct, f.criticality = r.tier";

constexpr std::string_view Q_ALGO_BRIDGE_FILES =
    "CALL algo.betweenness({nodeLabels: ['File'], relationshipTypes: ['IMPORTS'], samplingSize: 64, samplingSeed: 42})\n"
    "YIELD node, score\n"
    "WITH node, score\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({file: node.id, score: score}) AS j";

constexpr std::string_view Q_ALGO_FILE_COMPONENTS =
    "CALL algo.WCC({nodeLabels: ['File'], relationshipTypes: ['IMPORTS']}) YIELD node, componentId\n"
    "WITH componentId, collect(node.id) AS files\n"
    "RETURN toJSON({component: componentId, files: files}) AS j";

constexpr std::string_view Q_CRITICAL_SYMBOLS =
    "MATCH (f:Function)\n"
    "WHERE f.criticality = 'high'\n"
    "WITH f\n"
    "ORDER BY f.pagerank DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: f.id, signature: f.signature, pr_pct: f.pr_pct, fan_in: size([(x:Function)-[:CALLS]->(f) | 1])}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Briefing Reads
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_RECENT_SESSIONS =
    "MATCH (s:Session)\n"
    "WHERE s.id <> $exclude AND s.status IN ['ENDED', 'INTERRUPTED']\n"
    "WITH s\n"
    "ORDER BY s.started_at DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: s.id, client: s.client, status: s.status, started_at: s.started_at, ended_at: s.ended_at,\n"
    "               summary: s.summary, applied: s.applied_count, rejected: s.rejected_count,\n"
    "               intents: [(s)-[:HAS_INTENT]->(i:Intent) | {id: i.id, goal: i.goal, status: i.status}]}) AS j";

constexpr std::string_view Q_OPEN_INTENTS =
    "MATCH (s:Session)-[:HAS_INTENT]->(i:Intent {status: 'OPEN'})\n"
    "WHERE s.id <> $exclude_session\n"
    "WITH s, i\n"
    "ORDER BY i.updated_at DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: i.id, goal: i.goal, session: s.id, session_status: s.status, updated_at: i.updated_at,\n"
    "               last_decision: [(i)-[:RESOLVED_BY]->(d:Decision) | {status: d.status, ts: d.ts, files: d.files}]}) AS j";

constexpr std::string_view Q_TOP_PITFALLS =
    "MATCH (p:Pitfall)\n"
    "WITH p\n"
    "ORDER BY p.count DESC, p.last_seen DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({key: p.key, code: p.code, symbol_id: p.symbol_id, file: p.file, lesson: p.lesson, count: p.count,\n"
    "               unresolved: p.last_resolved < p.last_seen}) AS j";

constexpr std::string_view Q_HOT_SYMBOLS =
    "MATCH (d:Decision {status: 'APPLIED'})-[:MUTATED]->(s)\n"
    "WHERE d.ts >= $since\n"
    "WITH s, count(d) AS n, max(d.ts) AS last\n"
    "ORDER BY n DESC, last DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({id: s.id, changes: n, last: last}) AS j";

constexpr std::string_view Q_UNGATED_SINCE =
    "MATCH (d:Decision {actor: 'external'})\n"
    "WHERE d.ts >= $since\n"
    "WITH d\n"
    "ORDER BY d.ts DESC\n"
    "LIMIT 10\n"
    "RETURN toJSON({patch_id: d.patch_id, files: d.files, violation_codes: d.violation_codes, ts: d.ts}) AS j";

// ─────────────────────────────────────────────────────────────────────────────
// Recall (Fulltext)
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_RECALL_MEMORY =
    "CALL db.idx.fulltext.queryNodes('Memory', $q) YIELD node, score\n"
    "WITH node, score WHERE node.archived = false\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({type: 'memory', id: node.id, kind: node.kind, scope: node.scope, content: node.content,\n"
    "               anchors: node.anchors, updated_at: node.updated_at, score: score}) AS j";

constexpr std::string_view Q_RECALL_DECISIONS =
    "CALL db.idx.fulltext.queryNodes('Decision', $q) YIELD node, score\n"
    "WITH node, score\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({type: 'decision', id: node.patch_id, status: node.status, intent: node.intent, reasoning: node.reasoning,\n"
    "               files: node.files, symbols: node.symbols, ts: node.ts, score: score}) AS j";

constexpr std::string_view Q_RECALL_PITFALLS =
    "CALL db.idx.fulltext.queryNodes('Pitfall', $q) YIELD node, score\n"
    "WITH node, score\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({type: 'pitfall', id: node.key, code: node.code, lesson: node.lesson, symbol_id: node.symbol_id,\n"
    "               count: node.count, score: score}) AS j";

constexpr std::string_view Q_RECALL_SESSIONS =
    "CALL db.idx.fulltext.queryNodes('Session', $q) YIELD node, score\n"
    "WITH node, score\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({type: 'session', id: node.id, summary: node.summary, status: node.status, started_at: node.started_at, score: score}) AS j";

constexpr std::string_view Q_RECALL_INTENTS =
    "CALL db.idx.fulltext.queryNodes('Intent', $q) YIELD node, score\n"
    "WITH node, score\n"
    "ORDER BY score DESC\n"
    "LIMIT $limit\n"
    "RETURN toJSON({type: 'intent', id: node.id, goal: node.goal, status: node.status, updated_at: node.updated_at, score: score}) AS j";

constexpr std::string_view Q_RECALL_BY_ANCHOR =
    "MATCH (m:Memory) WHERE $id IN m.anchors AND m.archived = false\n"
    "RETURN toJSON({type: 'memory', id: m.id, kind: m.kind, scope: m.scope, content: m.content, updated_at: m.updated_at, score: 100.0}) AS j\n"
    "LIMIT $limit";

// ─────────────────────────────────────────────────────────────────────────────
// User Profile (User Graph)
// ─────────────────────────────────────────────────────────────────────────────

constexpr std::string_view Q_USER_PROFILE_UPSERT =
    "MERGE (u:UserProfile {id: 'local'})\n"
    "ON CREATE SET u.created_at = $now, u.workspaces = []\n"
    "SET u.workspaces = CASE WHEN $workspace IN u.workspaces THEN u.workspaces ELSE u.workspaces + [$workspace] END";

} // namespace gs::db
