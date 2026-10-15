#include "lang/syntax.hpp"

namespace gs::lang {

namespace {

void walk_syntax_errors(TSNode node, std::string_view src, size_t max_errors, std::vector<SyntaxError>& out) {
  if (out.size() >= max_errors) {
    return;
  }
  if (!ts_node_has_error(node)) {
    return;
  }

  if (ts_node_is_missing(node)) {
    TSPoint pt = ts_node_start_point(node);
    out.push_back({
        pt.row + 1,
        pt.column + 1,
        ts_node_start_byte(node),
        true,
        ts_node_type(node)
    });
    return;
  }

  if (ts_node_is_error(node)) {
    // Check if this error node contains deeper missing/error children
    bool has_error_child = false;
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      if (ts_node_has_error(ts_node_child(node, i))) {
        has_error_child = true;
        break;
      }
    }
    if (!has_error_child) {
      TSPoint pt = ts_node_start_point(node);
      out.push_back({
          pt.row + 1,
          pt.column + 1,
          ts_node_start_byte(node),
          false,
          ""
      });
      return;
    }
  }

  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    if (out.size() >= max_errors) {
      break;
    }
    walk_syntax_errors(ts_node_child(node, i), src, max_errors, out);
  }
}

} // namespace

std::vector<SyntaxError> collect_syntax_errors(TSNode root, std::string_view src, size_t max_errors) {
  std::vector<SyntaxError> errors;
  if (ts_node_is_null(root) || !ts_node_has_error(root)) {
    return errors;
  }
  walk_syntax_errors(root, src, max_errors, errors);
  return errors;
}

} // namespace gs::lang
