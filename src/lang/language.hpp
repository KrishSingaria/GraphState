#pragma once

#include <string_view>
#include <vector>
#include <tree_sitter/api.h>

extern "C" {
const TSLanguage* tree_sitter_python(void);
const TSLanguage* tree_sitter_typescript(void);
const TSLanguage* tree_sitter_tsx(void);
const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_go(void);
const TSLanguage* tree_sitter_rust(void);
const TSLanguage* tree_sitter_java(void);
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_json(void);
#if GS_WITH_CPP_GRAMMAR
const TSLanguage* tree_sitter_cpp(void);
#endif
}

namespace gs::lang {

enum class Language {
  Python,
  TypeScript,
  Tsx,
  JavaScript,
  Go,
  Rust,
  Java,
  C,
  Cpp,
  Json,
  Unknown
};

struct LanguageInfo {
  Language lang = Language::Unknown;
  std::string_view name = "unknown";
  const TSLanguage* ts = nullptr;
  bool semantic = false;
  std::vector<std::string_view> extensions;
};

class LanguageRegistry {
public:
  static const LanguageRegistry& get();

  const LanguageInfo& for_path(std::string_view rel_path) const;
  const LanguageInfo& for_name(std::string_view name) const;
  const LanguageInfo& info(Language l) const;
  const TSQuery* rule_query(Language lang, std::string_view rule_id) const;

private:
  LanguageRegistry();
  std::vector<LanguageInfo> languages_;
  LanguageInfo unknown_info_;
};

} // namespace gs::lang
