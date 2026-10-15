#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "lang/language.hpp"

namespace gs::lang {

struct ByteRange {
  uint32_t start = 0;
  uint32_t end = 0;

  bool operator==(const ByteRange& o) const = default;
};

struct LineRange {
  uint32_t start = 0; // 1-based inclusive
  uint32_t end = 0;   // 1-based inclusive

  bool operator==(const LineRange& o) const = default;
};

enum class ParamKind {
  PositionalOnly,
  PositionalOrKeyword,
  VarPositional,
  KeywordOnly,
  VarKeyword
};

std::string_view param_kind_name(ParamKind k);

struct Param {
  std::string name;
  ParamKind kind = ParamKind::PositionalOrKeyword;
  bool has_default = false;
  bool optional = false; // TS '?'
  std::string type;      // annotation text, whitespace-collapsed

  bool operator==(const Param& o) const = default;
};

struct Signature {
  std::vector<Param> params;
  bool is_async = false;
  std::string return_type;
  bool drop_first_for_instance_call = false; // Python methods (self/cls)
  bool has_overloads = false;                // TS overloads

  std::string canonical(std::string_view name = "") const;
  nlohmann::json to_json() const; // params_json representation
  int required_count() const;    // count of params that must be supplied
  int max_count() const;         // -1 if VarPositional, else max positional args

  bool operator==(const Signature& o) const = default;
};

enum class SymbolKind {
  Function,
  Method,
  Constructor,
  Arrow,
  Module,
  Class,
  Interface
};

std::string_view symbol_kind_name(SymbolKind k);

struct SymbolFact {
  std::string id;        // file + "::" + qualname (+ "#n" for duplicates)
  std::string name;
  std::string qualname;
  SymbolKind kind = SymbolKind::Function;
  std::string class_qualname; // owner class qualname for methods
  Signature sig;              // functions, methods, constructors, arrows
  std::vector<std::string> bases; // classes/interfaces only
  std::vector<std::string> decorators;
  bool is_exported = false;
  bool is_static = false;
  ByteRange def_range;
  ByteRange body_range;
  LineRange lines;
  uint32_t indent_col = 0; // byte column of def_range.start
  std::string body_hash;   // xxh3 of body with whitespace runs collapsed
  std::string doc;         // first docstring line <= 160 chars
  int complexity = 1;
  int max_nesting = 0;
  int loc = 0;
  std::vector<std::string> locals; // Python locals for UNDEFINED_NAME check
};

enum class CalleeShape {
  Name,
  Attr,
  SelfAttr,
  ClsAttr,
  SuperAttr,
  New,
  Other
};

std::string_view callee_shape_name(CalleeShape s);

struct CallFact {
  std::string caller_id; // enclosing symbol id (or <module>)
  CalleeShape shape = CalleeShape::Name;
  std::string name;      // final identifier: foo in foo(), x.foo(), self.foo()
  std::string receiver;  // "x" for x.foo(), "a.b" for a.b.foo(), "" otherwise
  uint32_t line = 0;     // 1-based
  uint32_t col = 0;      // 1-based
  uint32_t start_byte = 0;
  int pos_args = 0;
  std::vector<std::string> kw_names;
  bool has_star = false;  // *x in Python, spread in TS/JS
  bool has_dstar = false; // **x in Python
  std::string text;       // single-line snippet <= 120 chars
};

struct ImportFact {
  enum Kind {
    Module,
    FromNames,
    TsNamed,
    TsDefault,
    TsNamespace,
    TsSideEffect,
    ReExport
  } kind = Module;

  std::string spec; // python: "pkg.billing" / "..utils", ts: "./billing"
  std::vector<std::pair<std::string, std::string>> names; // (imported_name, local_alias)
  uint32_t line = 0;
  bool is_type_only = false; // TS 'import type'
};

std::string_view import_kind_name(ImportFact::Kind k);

struct SyntaxError {
  uint32_t line = 0; // 1-based
  uint32_t col = 0;  // 1-based
  uint32_t start_byte = 0;
  bool missing = false;
  std::string expected;
};

struct FileFacts {
  std::string file;
  std::string hash;
  Language lang = Language::Unknown;
  uint64_t size = 0;
  int64_t mtime_ns = 0;
  uint32_t line_count = 0;
  bool parse_ok = true;
  std::vector<SyntaxError> syntax_errors; // capped at 50
  std::vector<SymbolFact> symbols;        // includes <module> pseudo-symbol
  std::vector<CallFact> calls;
  std::vector<ImportFact> imports;
  std::vector<std::string> module_names;  // top-level names bound
  std::vector<std::string> exports;       // exported names
  std::string skipped_reason;

  const SymbolFact* find(std::string_view qualname_or_id) const;
  nlohmann::json to_json() const; // Complete FileFacts JSON serialization for goldens and CLI
};

} // namespace gs::lang
