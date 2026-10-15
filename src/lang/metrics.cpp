#include "lang/metrics.hpp"

#include <algorithm>
#include <string_view>

namespace gs::lang {

namespace {

bool is_nested_def(std::string_view type, Language lang) {
  if (lang == Language::Python) {
    return type == "function_definition" || type == "class_definition";
  } else {
    return type == "function_declaration" || type == "arrow_function" ||
           type == "function_expression" || type == "class_declaration";
  }
}

bool is_decision_node_python(std::string_view type) {
  return type == "if_statement" ||
         type == "elif_clause" ||
         type == "for_statement" ||
         type == "while_statement" ||
         type == "except_clause" ||
         type == "conditional_expression" ||
         type == "boolean_operator" ||
         type == "case_clause" ||
         type == "for_in_clause" ||
         type == "if_clause";
}

bool is_nesting_node_python(std::string_view type) {
  return type == "if_statement" ||
         type == "for_statement" ||
         type == "while_statement" ||
         type == "try_statement" ||
         type == "with_statement" ||
         type == "match_statement";
}

bool is_decision_node_ts(TSNode node, std::string_view type, std::string_view src) {
  if (type == "if_statement" ||
      type == "for_statement" ||
      type == "for_in_statement" ||
      type == "while_statement" ||
      type == "do_statement" ||
      type == "switch_case" ||
      type == "catch_clause" ||
      type == "ternary_expression") {
    return true;
  }
  if (type == "binary_expression") {
    // Check if operator is &&, ||, or ??
    TSNode op_node = ts_node_child_by_field_name(node, "operator", 8);
    if (!ts_node_is_null(op_node)) {
      uint32_t sb = ts_node_start_byte(op_node);
      uint32_t eb = ts_node_end_byte(op_node);
      if (sb < src.size() && eb <= src.size() && sb < eb) {
        std::string_view op = src.substr(sb, eb - sb);
        if (op == "&&" || op == "||" || op == "??") {
          return true;
        }
      }
    }
  }
  return false;
}

bool is_nesting_node_ts(std::string_view type) {
  return type == "if_statement" ||
         type == "for_statement" ||
         type == "for_in_statement" ||
         type == "while_statement" ||
         type == "do_statement" ||
         type == "try_statement" ||
         type == "switch_statement";
}

void walk_metrics(TSNode node, Language lang, std::string_view src, int current_depth, CodeMetrics& m) {
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; ++i) {
    TSNode child = ts_node_child(node, i);
    std::string_view type = ts_node_type(child);

    if (is_nested_def(type, lang)) {
      continue; // Skip nested function/class bodies completely
    }

    bool is_decision = false;
    bool is_nesting = false;

    if (lang == Language::Python) {
      is_decision = is_decision_node_python(type);
      is_nesting = is_nesting_node_python(type);
    } else {
      is_decision = is_decision_node_ts(child, type, src);
      is_nesting = is_nesting_node_ts(type);
    }

    if (is_decision) {
      m.complexity++;
    }

    int next_depth = current_depth;
    if (is_nesting) {
      next_depth++;
      if (next_depth > m.max_nesting) {
        m.max_nesting = next_depth;
      }
    }

    walk_metrics(child, lang, src, next_depth, m);
  }
}

} // namespace

CodeMetrics compute_metrics(TSNode body_node, Language lang, std::string_view src) {
  CodeMetrics m;
  m.complexity = 1;
  m.max_nesting = 0;
  if (ts_node_is_null(body_node)) {
    return m;
  }
  walk_metrics(body_node, lang, src, 0, m);
  return m;
}

} // namespace gs::lang
