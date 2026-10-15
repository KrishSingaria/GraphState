#include "lang/parser.hpp"

#include <fstream>
#include <sstream>
#include "platform/platform.hpp"

namespace gs::lang {

namespace {

thread_local ParserPtr tl_parser;

TSParser* get_thread_local_parser() {
  if (!tl_parser) {
    tl_parser.reset(ts_parser_new());
  }
  return tl_parser.get();
}

} // namespace

TreePtr parse_string(Language lang, std::string_view source) {
  const auto& info = LanguageRegistry::get().info(lang);
  if (!info.ts) {
    return nullptr;
  }
  TSParser* parser = get_thread_local_parser();
  if (!parser) {
    return nullptr;
  }
  ts_parser_set_language(parser, info.ts);
  TSTree* tree = ts_parser_parse_string(
      parser,
      nullptr,
      source.data(),
      static_cast<uint32_t>(source.size())
  );
  return TreePtr(tree);
}

TreePtr parse_file(const std::filesystem::path& path, Language lang) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f.is_open()) {
    return nullptr;
  }
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return parse_string(lang, content);
}

} // namespace gs::lang
