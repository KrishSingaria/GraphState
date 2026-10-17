#include "mcp/schema_validate.hpp"

#include <sstream>

namespace gs::mcp {

std::vector<ValidationError> validate_schema(
    const nlohmann::json& schema,
    nlohmann::json& instance,
    std::string_view path) {
  std::vector<ValidationError> errors;
  if (!schema.is_object()) return errors;

  std::string cur_path = std::string(path);
  if (cur_path.empty()) {
    cur_path = "/";
  }

  // ── type check ─────────────────────────────────────────────────────────────
  if (schema.contains("type") && schema["type"].is_string()) {
    const std::string expected_type = schema["type"].get<std::string>();
    bool type_matches = false;
    if (expected_type == "object") {
      type_matches = instance.is_object();
    } else if (expected_type == "string") {
      type_matches = instance.is_string();
    } else if (expected_type == "integer") {
      type_matches = instance.is_number_integer();
    } else if (expected_type == "number") {
      type_matches = instance.is_number();
    } else if (expected_type == "boolean") {
      type_matches = instance.is_boolean();
    } else if (expected_type == "array") {
      type_matches = instance.is_array();
    } else if (expected_type == "null") {
      type_matches = instance.is_null();
    }

    if (!type_matches) {
      errors.push_back({cur_path, "expected type '" + expected_type + "'"});
      return errors; // No deeper checking if type is completely mismatched
    }
  }

  // ── enum check ─────────────────────────────────────────────────────────────
  if (schema.contains("enum") && schema["enum"].is_array()) {
    bool found = false;
    for (const auto& allowed : schema["enum"]) {
      if (instance == allowed) {
        found = true;
        break;
      }
    }
    if (!found) {
      errors.push_back({cur_path, "value must be one of " + schema["enum"].dump()});
    }
  }

  // ── string constraints ─────────────────────────────────────────────────────
  if (instance.is_string()) {
    const std::string s = instance.get<std::string>();
    if (schema.contains("minLength") && schema["minLength"].is_number_integer()) {
      size_t min_len = schema["minLength"].get<size_t>();
      if (s.size() < min_len) {
        errors.push_back({cur_path, "string length must be >= " + std::to_string(min_len)});
      }
    }
    if (schema.contains("maxLength") && schema["maxLength"].is_number_integer()) {
      size_t max_len = schema["maxLength"].get<size_t>();
      if (s.size() > max_len) {
        errors.push_back({cur_path, "string length must be <= " + std::to_string(max_len)});
      }
    }
  }

  // ── number constraints ─────────────────────────────────────────────────────
  if (instance.is_number()) {
    if (schema.contains("minimum") && schema["minimum"].is_number()) {
      double min_val = schema["minimum"].get<double>();
      if (instance.get<double>() < min_val) {
        errors.push_back({cur_path, "must be >= " + schema["minimum"].dump()});
      }
    }
    if (schema.contains("maximum") && schema["maximum"].is_number()) {
      double max_val = schema["maximum"].get<double>();
      if (instance.get<double>() > max_val) {
        errors.push_back({cur_path, "must be <= " + schema["maximum"].dump()});
      }
    }
  }

  // ── array constraints ──────────────────────────────────────────────────────
  if (instance.is_array()) {
    if (schema.contains("minItems") && schema["minItems"].is_number_integer()) {
      size_t min_items = schema["minItems"].get<size_t>();
      if (instance.size() < min_items) {
        errors.push_back({cur_path, "array must have at least " + std::to_string(min_items) + " items"});
      }
    }
    if (schema.contains("maxItems") && schema["maxItems"].is_number_integer()) {
      size_t max_items = schema["maxItems"].get<size_t>();
      if (instance.size() > max_items) {
        errors.push_back({cur_path, "array must have at most " + std::to_string(max_items) + " items"});
      }
    }
    if (schema.contains("items") && schema["items"].is_object()) {
      const auto& item_schema = schema["items"];
      for (size_t i = 0; i < instance.size(); ++i) {
        std::string elem_path = (cur_path == "/" ? "" : cur_path) + "/" + std::to_string(i);
        auto item_errs = validate_schema(item_schema, instance[i], elem_path);
        errors.insert(errors.end(), item_errs.begin(), item_errs.end());
      }
    }
  }

  // ── object constraints ─────────────────────────────────────────────────────
  if (instance.is_object()) {
    // required fields check
    if (schema.contains("required") && schema["required"].is_array()) {
      for (const auto& req : schema["required"]) {
        if (req.is_string()) {
          const std::string prop_name = req.get<std::string>();
          if (!instance.contains(prop_name)) {
            std::string prop_path = (cur_path == "/" ? "" : cur_path) + "/" + prop_name;
            errors.push_back({prop_path, "field is required"});
          }
        }
      }
    }

    // properties and defaults
    if (schema.contains("properties") && schema["properties"].is_object()) {
      const auto& props = schema["properties"];
      for (auto it = props.begin(); it != props.end(); ++it) {
        const std::string& prop_name = it.key();
        const auto& prop_schema = it.value();
        std::string prop_path = (cur_path == "/" ? "" : cur_path) + "/" + prop_name;

        if (instance.contains(prop_name)) {
          auto prop_errs = validate_schema(prop_schema, instance[prop_name], prop_path);
          errors.insert(errors.end(), prop_errs.begin(), prop_errs.end());
        } else if (prop_schema.contains("default")) {
          // Inject default
          instance[prop_name] = prop_schema["default"];
        }
      }
    }

    // additionalProperties check
    if (schema.contains("additionalProperties")) {
      if (schema["additionalProperties"].is_boolean() && !schema["additionalProperties"].get<bool>()) {
        const auto& props = schema.value("properties", nlohmann::json::object());
        for (auto it = instance.begin(); it != instance.end(); ++it) {
          if (!props.contains(it.key())) {
            std::string prop_path = (cur_path == "/" ? "" : cur_path) + "/" + it.key();
            errors.push_back({prop_path, "additional property is not allowed"});
          }
        }
      }
    }
  }

  return errors;
}

} // namespace gs::mcp
