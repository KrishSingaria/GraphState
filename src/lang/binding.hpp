#pragma once

#include <string>
#include <vector>
#include "lang/facts.hpp"
#include "lang/language.hpp"

namespace gs::lang {

enum class CallKind {
  Plain,
  InstanceMethod,
  ClassMethodViaClass,
  Constructor,
  Unbound
};

struct CallSite {
  int pos_args = 0;
  std::vector<std::string> kw_names;
  bool has_star = false;
  bool has_dstar = false;
};

struct BindResult {
  enum Verdict {
    Ok,
    Incompatible,
    Unknown
  } verdict = Ok;

  std::string reason;

  static BindResult ok() {
    return {Ok, ""};
  }

  static BindResult incompatible(std::string r) {
    return {Incompatible, std::move(r)};
  }

  static BindResult unknown(std::string r = "") {
    return {Unknown, std::move(r)};
  }
};

class BindingChecker {
public:
  static BindResult check(const Signature& sig, const CallSite& call, CallKind kind, Language lang);
};

} // namespace gs::lang
