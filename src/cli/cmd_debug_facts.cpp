#include "cli/cli.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include "lang/extractor.hpp"
#include "lang/language.hpp"

namespace gs::cli {

int cmd_debug_facts(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: graphstate debug-facts <file> [--lang <name>]\n";
    return 1;
  }

  std::string file_path = argv[2];
  std::string lang_override;

  for (int i = 3; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--lang" && i + 1 < argc) {
      lang_override = argv[++i];
    }
  }

  std::ifstream f(file_path, std::ios::in | std::ios::binary);
  if (!f.is_open()) {
    std::cerr << "Error: could not read file: " << file_path << "\n";
    return 1;
  }
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

  lang::Language lang = lang::Language::Unknown;
  if (!lang_override.empty()) {
    lang = lang::LanguageRegistry::get().for_name(lang_override).lang;
  } else {
    lang = lang::LanguageRegistry::get().for_path(file_path).lang;
  }

  // Normalize path for graph consistency (replace backslashes with slashes)
  std::string norm_path = file_path;
  std::replace(norm_path.begin(), norm_path.end(), '\\', '/');

  lang::FileFacts facts = lang::extract_any(norm_path, content, lang);

  std::cout << facts.to_json().dump(2) << "\n";
  return 0;
}

} // namespace gs::cli
