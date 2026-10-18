#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <tree_sitter/api.h>
#include "lang/facts.hpp"

namespace gs::lang::ts {

std::string_view node_text(TSNode node, std::string_view src);
std::string_view trim_str(std::string_view s);
std::string strip_quotes(std::string_view s);
std::string extract_jsdoc(TSNode node, std::string_view src);

// Formal parameters parsing shared between TS/TSX and JS
void parse_parameters(TSNode params_node, std::vector<Param>& out, std::string_view src, bool is_typescript);

// Call and new expressions parsing
void extract_call(TSNode call_node, const std::string& caller_id, std::vector<CallFact>& calls, std::string_view src);
void extract_new(TSNode new_node, const std::string& caller_id, std::vector<CallFact>& calls, std::string_view src);

// Recursively collects calls and new expressions inside a node (skipping nested class declarations)
void collect_calls(TSNode node, const std::string& caller_id, std::vector<CallFact>& calls, std::string_view src);

// Import and export parsing
void extract_import(TSNode import_node, std::vector<ImportFact>& imports, std::string_view src);
void extract_export_clause_or_reexport(TSNode export_node, std::vector<ImportFact>& imports, std::vector<std::string>& exports, std::string_view src);
void extract_commonjs(TSNode node, std::vector<ImportFact>& imports, std::vector<std::string>& exports, std::string_view src);

// Duplicate qualname resolver
void resolve_duplicate_ids(std::vector<SymbolFact>& symbols, const std::string& file);

} // namespace gs::lang::ts

