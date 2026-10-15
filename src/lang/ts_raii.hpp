#pragma once

#include <memory>
#include <tree_sitter/api.h>

namespace gs::lang {

struct TSParserDeleter {
  void operator()(TSParser* p) const noexcept {
    if (p) {
      ts_parser_delete(p);
    }
  }
};

struct TSTreeDeleter {
  void operator()(TSTree* t) const noexcept {
    if (t) {
      ts_tree_delete(t);
    }
  }
};

struct TSQueryDeleter {
  void operator()(TSQuery* q) const noexcept {
    if (q) {
      ts_query_delete(q);
    }
  }
};

struct TSQueryCursorDeleter {
  void operator()(TSQueryCursor* qc) const noexcept {
    if (qc) {
      ts_query_cursor_delete(qc);
    }
  }
};

using ParserPtr = std::unique_ptr<TSParser, TSParserDeleter>;
using TreePtr = std::unique_ptr<TSTree, TSTreeDeleter>;
using QueryPtr = std::unique_ptr<TSQuery, TSQueryDeleter>;
using QueryCursorPtr = std::unique_ptr<TSQueryCursor, TSQueryCursorDeleter>;

} // namespace gs::lang
