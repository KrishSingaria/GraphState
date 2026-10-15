#pragma once

#include <memory>
#include <string>
#include <tree_sitter/api.h>
#include "lang/facts.hpp"
#include "lang/language.hpp"

namespace gs::lang {

class Extractor {
public:
  virtual ~Extractor() = default;
  virtual FileFacts extract(const std::string& file, const std::string& src, TSTree* tree) const = 0;
};

std::unique_ptr<Extractor> make_extractor(Language lang);

// High-level extraction for any language:
// Semantic languages -> parsed + extracted symbols/calls/imports.
// Syntax-only languages -> parsed + syntax errors + file metrics.
// Unknown -> basic FileFacts with size/hash only.
FileFacts extract_any(const std::string& file, const std::string& src, Language lang);

} // namespace gs::lang
