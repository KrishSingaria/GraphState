#include <doctest/doctest.h>
#include "lang/binding.hpp"

using namespace gs::lang;

TEST_CASE("BindingChecker validates call signatures") {
  SUBCASE("Python: simple positional and default arguments") {
    Signature sig;
    sig.params = {
      {"a", ParamKind::PositionalOrKeyword, false, false, "int"},
      {"b", ParamKind::PositionalOrKeyword, true, false, "int"} // default
    };

    // Correct: 1 arg
    CallSite c1{1, {}, false, false};
    auto r1 = BindingChecker::check(sig, c1, CallKind::Plain, Language::Python);
    CHECK(r1.verdict == BindResult::Ok);

    // Correct: 2 args
    CallSite c2{2, {}, false, false};
    auto r2 = BindingChecker::check(sig, c2, CallKind::Plain, Language::Python);
    CHECK(r2.verdict == BindResult::Ok);

    // Error: 0 args (missing 'a')
    CallSite c0{0, {}, false, false};
    auto r0 = BindingChecker::check(sig, c0, CallKind::Plain, Language::Python);
    CHECK(r0.verdict == BindResult::Incompatible);
    CHECK(r0.reason.find("missing required argument 'a'") != std::string::npos);

    // Error: 3 args (takes 2 positional arguments but 3 were given)
    CallSite c3{3, {}, false, false};
    auto r3 = BindingChecker::check(sig, c3, CallKind::Plain, Language::Python);
    CHECK(r3.verdict == BindResult::Incompatible);
    CHECK(r3.reason.find("takes 2 positional arguments but 3 were given") != std::string::npos);
  }

  SUBCASE("Python: keyword-only and positional-only arguments") {
    Signature sig;
    sig.params = {
      {"pos_only", ParamKind::PositionalOnly, false, false, "int"},
      {"normal", ParamKind::PositionalOrKeyword, false, false, "int"},
      {"kw_only", ParamKind::KeywordOnly, false, false, "int"}
    };

    // Valid: 2 pos + 1 kw
    CallSite c_ok{2, {"kw_only"}, false, false};
    auto r_ok = BindingChecker::check(sig, c_ok, CallKind::Plain, Language::Python);
    CHECK(r_ok.verdict == BindResult::Ok);

    // Invalid: keyword passed for positional-only
    CallSite c_pos_as_kw{1, {"pos_only", "kw_only"}, false, false};
    auto r_pos_as_kw = BindingChecker::check(sig, c_pos_as_kw, CallKind::Plain, Language::Python);
    CHECK(r_pos_as_kw.verdict == BindResult::Incompatible);
    CHECK(r_pos_as_kw.reason.find("unexpected keyword argument 'pos_only'") != std::string::npos);

    // Invalid: positional passed for keyword-only (takes 2 positional arguments but 3 were given)
    CallSite c_too_many_pos{3, {}, false, false};
    auto r_too_many_pos = BindingChecker::check(sig, c_too_many_pos, CallKind::Plain, Language::Python);
    CHECK(r_too_many_pos.verdict == BindResult::Incompatible);

    // Invalid: multiple values for argument
    CallSite c_dup{2, {"normal", "kw_only"}, false, false};
    auto r_dup = BindingChecker::check(sig, c_dup, CallKind::Plain, Language::Python);
    CHECK(r_dup.verdict == BindResult::Incompatible);
    CHECK(r_dup.reason.find("multiple values for argument 'normal'") != std::string::npos);
  }

  SUBCASE("Python: instance methods drop first self parameter") {
    Signature sig;
    sig.drop_first_for_instance_call = true;
    sig.params = {
      {"self", ParamKind::PositionalOrKeyword, false, false, ""},
      {"x", ParamKind::PositionalOrKeyword, false, false, "int"}
    };

    // Called as instance method: self is dropped -> takes 1 argument
    CallSite c_inst{1, {}, false, false};
    auto r_inst = BindingChecker::check(sig, c_inst, CallKind::InstanceMethod, Language::Python);
    CHECK(r_inst.verdict == BindResult::Ok);

    // Called as unbound function (e.g. Cls.method(obj, 10)): self is NOT dropped -> takes 2 arguments
    CallSite c_unbound{2, {}, false, false};
    auto r_unbound = BindingChecker::check(sig, c_unbound, CallKind::Unbound, Language::Python);
    CHECK(r_unbound.verdict == BindResult::Ok);

    // Calling unbound with 1 arg fails
    auto r_unbound_fail = BindingChecker::check(sig, c_inst, CallKind::Unbound, Language::Python);
    CHECK(r_unbound_fail.verdict == BindResult::Incompatible);
  }

  SUBCASE("TypeScript: optional arguments and rest parameters") {
    Signature sig;
    sig.params = {
      {"x", ParamKind::PositionalOrKeyword, false, false, "number"},
      {"y", ParamKind::PositionalOrKeyword, false, true, "string"}, // optional ?
      {"z", ParamKind::PositionalOrKeyword, true, false, "boolean"}  // default
    };

    // 1 arg (required_min = 1, max = 3)
    CallSite c1{1, {}, false, false};
    CHECK(BindingChecker::check(sig, c1, CallKind::Plain, Language::TypeScript).verdict == BindResult::Ok);

    // 2 args
    CallSite c2{2, {}, false, false};
    CHECK(BindingChecker::check(sig, c2, CallKind::Plain, Language::TypeScript).verdict == BindResult::Ok);

    // 3 args
    CallSite c3{3, {}, false, false};
    CHECK(BindingChecker::check(sig, c3, CallKind::Plain, Language::TypeScript).verdict == BindResult::Ok);

    // 0 args -> fails
    CallSite c0{0, {}, false, false};
    auto r0 = BindingChecker::check(sig, c0, CallKind::Plain, Language::TypeScript);
    CHECK(r0.verdict == BindResult::Incompatible);
    CHECK(r0.reason.find("expected at least 1 arguments, got 0") != std::string::npos);

    // 4 args -> fails
    CallSite c4{4, {}, false, false};
    auto r4 = BindingChecker::check(sig, c4, CallKind::Plain, Language::TypeScript);
    CHECK(r4.verdict == BindResult::Incompatible);
    CHECK(r4.reason.find("expected at most 3 arguments, got 4") != std::string::npos);
  }

  SUBCASE("TypeScript: overloads and splats bypass checks") {
    Signature sig;
    sig.has_overloads = true;
    sig.params = {{"a", ParamKind::PositionalOrKeyword, false, false, "number"}};

    CallSite c{0, {}, false, false};
    auto r = BindingChecker::check(sig, c, CallKind::Plain, Language::TypeScript);
    CHECK(r.verdict == BindResult::Unknown);

    Signature sig2;
    sig2.params = {{"a", ParamKind::PositionalOrKeyword, false, false, "number"}};
    CallSite c_splat{0, {}, true, false}; // has_star
    auto r_splat = BindingChecker::check(sig2, c_splat, CallKind::Plain, Language::TypeScript);
    CHECK(r_splat.verdict == BindResult::Unknown);
  }
}
