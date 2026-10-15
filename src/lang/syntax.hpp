#pragma once

#include <string_view>
#include <vector>
#include <tree_sitter/api.h>
#include "lang/facts.hpp"

namespace gs::lang {

// Collects syntax errors (missing tokens and unexpected syntax) from a Tree-sitter root node.
// Descends only into subtrees where ts_node_has_error(n) is true.
// Caps results at max_errors (default 50).
std::vector<SyntaxError> collect_syntax_errors(TSNode root, std::string_view src, size_t max_errors = 50);

} // namespace gs::lang
