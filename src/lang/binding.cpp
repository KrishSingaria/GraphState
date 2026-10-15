#include "lang/binding.hpp"

#include <algorithm>
#include <set>

namespace gs::lang {

BindResult BindingChecker::check(const Signature& sig, const CallSite& call, CallKind kind, Language lang) {
  // 1. Overloads bypass checks
  if (sig.has_overloads) {
    return BindResult::unknown("function has overloads");
  }

  // 2. Star / splat arguments in call cannot be verified statically without types/values
  if (call.has_star || call.has_dstar) {
    return BindResult::unknown("call contains splat arguments");
  }

  if (lang == Language::Python) {
    std::vector<Param> ps = sig.params;

    // Drop first parameter (self/cls) for instance method or constructor call
    if (sig.drop_first_for_instance_call &&
        (kind == CallKind::InstanceMethod || kind == CallKind::Constructor || kind == CallKind::ClassMethodViaClass)) {
      if (!ps.empty()) {
        ps.erase(ps.begin());
      }
    }

    std::vector<const Param*> positional;
    bool has_var_pos = false;
    bool has_var_kw = false;

    for (const auto& p : ps) {
      if (p.kind == ParamKind::PositionalOnly || p.kind == ParamKind::PositionalOrKeyword) {
        positional.push_back(&p);
      } else if (p.kind == ParamKind::VarPositional) {
        has_var_pos = true;
      } else if (p.kind == ParamKind::VarKeyword) {
        has_var_kw = true;
      }
    }

    // Check positional argument count
    if (call.pos_args > static_cast<int>(positional.size()) && !has_var_pos) {
      return BindResult::incompatible("takes " + std::to_string(positional.size()) +
                                      " positional arguments but " + std::to_string(call.pos_args) +
                                      " were given");
    }

    std::set<std::string> bound;
    int bounded_pos = std::min(call.pos_args, static_cast<int>(positional.size()));
    for (int i = 0; i < bounded_pos; ++i) {
      bound.insert(positional[i]->name);
    }

    // Check keyword arguments
    for (const auto& k : call.kw_names) {
      const Param* matching = nullptr;
      const Param* pos_only_match = nullptr;

      for (const auto& p : ps) {
        if (p.name == k) {
          if (p.kind == ParamKind::PositionalOrKeyword || p.kind == ParamKind::KeywordOnly) {
            matching = &p;
            break;
          } else if (p.kind == ParamKind::PositionalOnly) {
            pos_only_match = &p;
          }
        }
      }

      if (!matching) {
        if (has_var_kw) {
          // Absorbed by **kwargs
        } else if (pos_only_match) {
          return BindResult::incompatible("unexpected keyword argument '" + k + "'");
        } else {
          return BindResult::incompatible("unexpected keyword argument '" + k + "'");
        }
      } else {
        if (bound.count(k)) {
          return BindResult::incompatible("multiple values for argument '" + k + "'");
        }
        bound.insert(k);
      }
    }

    // Check required parameters
    for (const auto& p : ps) {
      if (p.kind != ParamKind::VarPositional && p.kind != ParamKind::VarKeyword && !p.has_default) {
        if (!bound.count(p.name)) {
          return BindResult::incompatible("missing required argument '" + p.name + "'");
        }
      }
    }

    return BindResult::ok();
  }

  if (lang == Language::TypeScript || lang == Language::Tsx || lang == Language::JavaScript) {
    std::vector<Param> ps = sig.params;

    // Drop leading 'this' parameter
    if (!ps.empty() && ps.front().name == "this") {
      ps.erase(ps.begin());
    }

    // Calculate required_min
    int required_min = 0;
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
      if (!ps[i].optional && !ps[i].has_default && ps[i].kind != ParamKind::VarPositional) {
        required_min = i + 1;
      }
    }

    // Calculate max
    bool has_var_pos = false;
    for (const auto& p : ps) {
      if (p.kind == ParamKind::VarPositional) {
        has_var_pos = true;
        break;
      }
    }

    int max_args = has_var_pos ? -1 : static_cast<int>(ps.size());

    if (call.pos_args < required_min) {
      return BindResult::incompatible("expected at least " + std::to_string(required_min) +
                                      " arguments, got " + std::to_string(call.pos_args));
    }

    if (max_args != -1 && call.pos_args > max_args) {
      return BindResult::incompatible("expected at most " + std::to_string(max_args) +
                                      " arguments, got " + std::to_string(call.pos_args));
    }

    return BindResult::ok();
  }

  return BindResult::ok();
}

} // namespace gs::lang
