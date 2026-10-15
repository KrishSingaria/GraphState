#include "lang/extractor.hpp"

#include <algorithm>
#include "core/hash.hpp"
#include "lang/parser.hpp"
#include "lang/syntax.hpp"

namespace gs::lang {

// Forward declarations of language-specific extractors
std::unique_ptr<Extractor> make_python_extractor();
std::unique_ptr<Extractor> make_ts_extractor(Language lang);
std::unique_ptr<Extractor> make_js_extractor();

std::unique_ptr<Extractor> make_extractor(Language lang) {
  switch (lang) {
  case Language::Python:
    return make_python_extractor();
  case Language::TypeScript:
  case Language::Tsx:
    return make_ts_extractor(lang);
  case Language::JavaScript:
    return make_js_extractor();
  default:
    return nullptr;
  }
}

FileFacts extract_any(const std::string& file, const std::string& src, Language lang) {
  FileFacts facts;
  facts.file = file;
  facts.hash = core::xxh3_hex(src);
  facts.lang = lang;
  facts.size = src.size();

  // Count lines
  uint32_t line_cnt = 0;
  if (!src.empty()) {
    line_cnt = 1;
    for (char c : src) {
      if (c == '\n') line_cnt++;
    }
  }
  facts.line_count = line_cnt;

  if (lang == Language::Unknown) {
    facts.parse_ok = true;
    return facts;
  }

  // Parse using thread-local parser pool
  TreePtr tree = parse_string(lang, src);
  if (!tree) {
    facts.parse_ok = false;
    return facts;
  }

  auto extractor = make_extractor(lang);
  if (extractor) {
    // Semantic language
    return extractor->extract(file, src, tree.get());
  }

  // Syntax-only language
  facts.parse_ok = true;
  facts.syntax_errors = collect_syntax_errors(ts_tree_root_node(tree.get()), src);
  return facts;
}

} // namespace gs::lang
