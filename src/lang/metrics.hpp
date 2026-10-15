#pragma once

#include <tree_sitter/api.h>
#include "lang/language.hpp"

namespace gs::lang {

struct CodeMetrics {
  int complexity = 1;
  int max_nesting = 0;
};

// Computes cyclomatic complexity and max nesting depth for a symbol body AST node.
CodeMetrics compute_metrics(TSNode body_node, Language lang, std::string_view src);

} // namespace gs::lang
