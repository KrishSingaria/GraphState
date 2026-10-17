#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace gs::mcp {

struct ValidationError {
  std::string path;    // JSON pointer, e.g. "/query" or "/edits/2/op"
  std::string message; // Human-readable violation description

  std::string format() const {
    if (path.empty() || path == "/") {
      return message;
    }
    return path + ": " + message;
  }
};

// Validates `instance` against the JSON Schema subset and populates default values if missing.
// Returns a list of validation errors (empty if valid).
std::vector<ValidationError> validate_schema(
    const nlohmann::json& schema,
    nlohmann::json& instance,
    std::string_view path = "");

} // namespace gs::mcp
