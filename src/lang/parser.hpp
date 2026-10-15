#pragma once

#include <filesystem>
#include <string_view>
#include "lang/language.hpp"
#include "lang/ts_raii.hpp"

namespace gs::lang {

// Parses source code in memory with thread_local parser reuse.
TreePtr parse_string(Language lang, std::string_view source);

// Parses a source file from disk.
TreePtr parse_file(const std::filesystem::path& path, Language lang);

} // namespace gs::lang
