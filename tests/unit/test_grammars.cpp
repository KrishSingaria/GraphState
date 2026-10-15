#include <doctest/doctest.h>
#include <tree_sitter/api.h>
#include <vector>

extern "C" {
const TSLanguage *tree_sitter_python(void);
const TSLanguage *tree_sitter_typescript(void);
const TSLanguage *tree_sitter_tsx(void);
const TSLanguage *tree_sitter_javascript(void);
const TSLanguage *tree_sitter_go(void);
const TSLanguage *tree_sitter_rust(void);
const TSLanguage *tree_sitter_java(void);
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_json(void);
#if GS_WITH_CPP_GRAMMAR
const TSLanguage *tree_sitter_cpp(void);
#endif
}

TEST_CASE("Day-1 Verification V9 & V12: tree-sitter core + all grammars ABI and loading") {
  TSParser* parser = ts_parser_new();
  REQUIRE(parser != nullptr);

  struct GrammarCheck {
    const char* name;
    const TSLanguage* (*fn)(void);
  };

  std::vector<GrammarCheck> grammars = {
    {"python", tree_sitter_python},
    {"typescript", tree_sitter_typescript},
    {"tsx", tree_sitter_tsx},
    {"javascript", tree_sitter_javascript},
    {"go", tree_sitter_go},
    {"rust", tree_sitter_rust},
    {"java", tree_sitter_java},
    {"c", tree_sitter_c},
    {"json", tree_sitter_json},
#if GS_WITH_CPP_GRAMMAR
    {"cpp", tree_sitter_cpp},
#endif
  };

  for (const auto& g : grammars) {
    CAPTURE(g.name);
    const TSLanguage* lang = g.fn();
    REQUIRE(lang != nullptr);

    uint32_t abi_version = ts_language_abi_version(lang);
    CHECK(abi_version >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
    CHECK(abi_version <= TREE_SITTER_LANGUAGE_VERSION);

    bool ok = ts_parser_set_language(parser, lang);
    CHECK(ok == true);
  }

  ts_parser_delete(parser);
}
