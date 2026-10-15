#include "lang/language.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

namespace gs::lang {

namespace {

std::string to_lower_str(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool ends_with_ignore_case(std::string_view str, std::string_view suffix) {
  if (str.size() < suffix.size()) {
    return false;
  }
  std::string_view tail = str.substr(str.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

std::string get_extension(std::string_view path) {
  size_t last_slash = path.find_last_of("/\\");
  std::string_view filename = (last_slash == std::string_view::npos) ? path : path.substr(last_slash + 1);
  size_t dot = filename.find_last_of('.');
  if (dot == std::string_view::npos) {
    return "";
  }
  return to_lower_str(filename.substr(dot));
}

} // namespace

LanguageRegistry::LanguageRegistry() {
  unknown_info_ = LanguageInfo{
      Language::Unknown,
      "unknown",
      nullptr,
      false,
      {}
  };

  languages_.push_back(LanguageInfo{
      Language::Python,
      "python",
      tree_sitter_python(),
      true,
      {".py", ".pyi"}
  });

  languages_.push_back(LanguageInfo{
      Language::TypeScript,
      "typescript",
      tree_sitter_typescript(),
      true,
      {".ts", ".mts", ".cts", ".d.ts"}
  });

  languages_.push_back(LanguageInfo{
      Language::Tsx,
      "tsx",
      tree_sitter_tsx(),
      true,
      {".tsx"}
  });

  languages_.push_back(LanguageInfo{
      Language::JavaScript,
      "javascript",
      tree_sitter_javascript(),
      true,
      {".js", ".mjs", ".cjs", ".jsx"}
  });

  languages_.push_back(LanguageInfo{
      Language::Go,
      "go",
      tree_sitter_go(),
      false,
      {".go"}
  });

  languages_.push_back(LanguageInfo{
      Language::Rust,
      "rust",
      tree_sitter_rust(),
      false,
      {".rs"}
  });

  languages_.push_back(LanguageInfo{
      Language::Java,
      "java",
      tree_sitter_java(),
      false,
      {".java"}
  });

  languages_.push_back(LanguageInfo{
      Language::C,
      "c",
      tree_sitter_c(),
      false,
      {".c", ".h"}
  });

#if GS_WITH_CPP_GRAMMAR
  languages_.push_back(LanguageInfo{
      Language::Cpp,
      "cpp",
      tree_sitter_cpp(),
      false,
      {".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx"}
  });
#else
  languages_.push_back(LanguageInfo{
      Language::Cpp,
      "cpp",
      nullptr,
      false,
      {".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx"}
  });
#endif

  languages_.push_back(LanguageInfo{
      Language::Json,
      "json",
      tree_sitter_json(),
      false,
      {".json"}
  });

  // Verify ABI compatibility [13, 15]
  for (const auto& info : languages_) {
    if (info.ts) {
      uint32_t abi = ts_language_abi_version(info.ts);
      if (abi < 13 || abi > 15) {
        std::cerr << "Warning: Language " << info.name << " ABI version " << abi << " outside [13, 15]\n";
      }
    }
  }
}

const LanguageRegistry& LanguageRegistry::get() {
  static LanguageRegistry registry;
  return registry;
}

const LanguageInfo& LanguageRegistry::for_path(std::string_view rel_path) const {
  if (ends_with_ignore_case(rel_path, ".d.ts")) {
    return info(Language::TypeScript);
  }
  std::string ext = get_extension(rel_path);
  if (ext.empty()) {
    return unknown_info_;
  }
  for (const auto& item : languages_) {
    for (std::string_view known_ext : item.extensions) {
      if (ext == known_ext) {
        return item;
      }
    }
  }
  return unknown_info_;
}

const LanguageInfo& LanguageRegistry::for_name(std::string_view name) const {
  std::string lower = to_lower_str(name);
  for (const auto& item : languages_) {
    if (item.name == lower) {
      return item;
    }
  }
  return unknown_info_;
}

const LanguageInfo& LanguageRegistry::info(Language l) const {
  for (const auto& item : languages_) {
    if (item.lang == l) {
      return item;
    }
  }
  return unknown_info_;
}

const TSQuery* LanguageRegistry::rule_query(Language, std::string_view) const {
  return nullptr;
}

} // namespace gs::lang
