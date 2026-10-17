#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "gate/rules.hpp"
#include "lang/extractor.hpp"

using namespace gs;
using namespace gs::gate;

TEST_CASE("RulesEngine: Built-in quality rules") {
  RulesEngine engine;

  SUBCASE("py-mutable-default-arg") {
    std::string bad_code =
R"(def test_fn(x, items=[]):
    return len(items)
)";
    auto facts = lang::extract_any("sample.py", bad_code, lang::Language::Python);
    auto violations = engine.check("sample.py", lang::Language::Python, bad_code, facts, {});

    bool found = false;
    for (const auto& v : violations) {
      if (v.rule_id.value_or("") == "py-mutable-default-arg") {
        found = true;
        CHECK(v.severity == Severity::Error);
        CHECK(v.message.find("items") != std::string::npos);
      }
    }
    CHECK(found);

    // Good code with None
    std::string good_code =
R"(def test_fn(x, items=None):
    if items is None: items = []
    return len(items)
)";
    auto good_facts = lang::extract_any("sample.py", good_code, lang::Language::Python);
    auto good_violations = engine.check("sample.py", lang::Language::Python, good_code, good_facts, {});
    bool good_found = false;
    for (const auto& v : good_violations) {
      if (v.rule_id.value_or("") == "py-mutable-default-arg") good_found = true;
    }
    CHECK(!good_found);
  }

  SUBCASE("py-bare-except") {
    std::string bad_code =
R"(def process():
    try:
        call()
    except:
        pass
)";
    auto facts = lang::extract_any("sample.py", bad_code, lang::Language::Python);
    auto violations = engine.check("sample.py", lang::Language::Python, bad_code, facts, {});

    bool found = false;
    for (const auto& v : violations) {
      if (v.rule_id.value_or("") == "py-bare-except") {
        found = true;
        CHECK(v.severity == Severity::Error);
      }
    }
    CHECK(found);

    // Safe except Exception
    std::string good_code =
R"(def process():
    try:
        call()
    except Exception:
        handle()
)";
    auto good_facts = lang::extract_any("sample.py", good_code, lang::Language::Python);
    auto good_violations = engine.check("sample.py", lang::Language::Python, good_code, good_facts, {});
    bool good_found = false;
    for (const auto& v : good_violations) {
      if (v.rule_id.value_or("") == "py-bare-except") good_found = true;
    }
    CHECK(!good_found);
  }

  SUBCASE("no-debug-print in production vs test code") {
    std::string code =
R"(def run():
    print("debugging here")
)";
    auto facts = lang::extract_any("app.py", code, lang::Language::Python);

    // In app.py -> should warn
    auto prod_violations = engine.check("app.py", lang::Language::Python, code, facts, {});
    bool found_in_prod = false;
    for (const auto& v : prod_violations) {
      if (v.rule_id.value_or("") == "no-debug-print") found_in_prod = true;
    }
    CHECK(found_in_prod);

    // In tests/test_app.py -> excluded, should not warn
    auto test_violations = engine.check("tests/test_app.py", lang::Language::Python, code, facts, {});
    bool found_in_test = false;
    for (const auto& v : test_violations) {
      if (v.rule_id.value_or("") == "no-debug-print") found_in_test = true;
    }
    CHECK(!found_in_test);
  }

  SUBCASE("ts-no-explicit-any") {
    std::string code =
R"(function process(val: any): void {
    return;
}
)";
    auto facts = lang::extract_any("app.ts", code, lang::Language::TypeScript);
    auto violations = engine.check("app.ts", lang::Language::TypeScript, code, facts, {});

    bool found = false;
    for (const auto& v : violations) {
      if (v.rule_id.value_or("") == "ts-no-explicit-any") found = true;
    }
    CHECK(found);
  }

  SUBCASE("no-empty-catch") {
    std::string py_code =
R"(def run():
    try:
        do_work()
    except Exception:
        pass
)";
    auto py_facts = lang::extract_any("app.py", py_code, lang::Language::Python);
    auto py_violations = engine.check("app.py", lang::Language::Python, py_code, py_facts, {});

    bool found_py = false;
    for (const auto& v : py_violations) {
      if (v.rule_id.value_or("") == "no-empty-catch") found_py = true;
    }
    CHECK(found_py);

    std::string ts_code =
R"(function run() {
    try {
        doWork();
    } catch (e) {
    }
}
)";
    auto ts_facts = lang::extract_any("app.ts", ts_code, lang::Language::TypeScript);
    auto ts_violations = engine.check("app.ts", lang::Language::TypeScript, ts_code, ts_facts, {});

    bool found_ts = false;
    for (const auto& v : ts_violations) {
      if (v.rule_id.value_or("") == "no-empty-catch") found_ts = true;
    }
    CHECK(found_ts);
  }
}
