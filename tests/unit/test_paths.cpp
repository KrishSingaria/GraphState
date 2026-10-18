#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/paths.hpp"
#include "core/config.hpp"
#include "platform/platform.hpp"

#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace gs;
using namespace gs::core;

TEST_SUITE("core::paths") {

TEST_CASE("glob_match: exact literal matches") {
  CHECK(glob_match("src/core/paths.cpp", "src/core/paths.cpp"));
  CHECK_FALSE(glob_match("src/core/paths.cpp", "src/core/paths.hpp"));
  CHECK_FALSE(glob_match("src/core/paths.cpp", "src/core/other.cpp"));
  CHECK(glob_match("", ""));
  CHECK_FALSE(glob_match("", "file.txt"));
}

TEST_CASE("glob_match: single wildcard '*'") {
  CHECK(glob_match("*.py", "app.py"));
  CHECK(glob_match("*.py", "models.py"));
  CHECK(glob_match("test_*.cpp", "test_paths.cpp"));
  CHECK(glob_match("test_*.cpp", "test_.cpp"));

  CHECK_FALSE(glob_match("*.py", "src/app.py"));
  CHECK_FALSE(glob_match("*.py", "pkg/sub/app.py"));
  CHECK(glob_match("src/*.py", "src/app.py"));
  CHECK_FALSE(glob_match("src/*.py", "src/sub/app.py"));
}

TEST_CASE("glob_match: question mark '?'") {
  CHECK(glob_match("file?.txt", "file1.txt"));
  CHECK(glob_match("file?.txt", "fileA.txt"));
  CHECK_FALSE(glob_match("file?.txt", "file.txt"));
  CHECK_FALSE(glob_match("file?.txt", "file12.txt"));
  CHECK_FALSE(glob_match("file?.txt", "file/.txt"));
}

TEST_CASE("glob_match: case insensitivity folding") {
  CHECK_FALSE(glob_match("FOO.TXT", "foo.txt", false));
  CHECK(glob_match("FOO.TXT", "foo.txt", true));
  CHECK(glob_match("*.py", "App.PY", true));
  CHECK(glob_match("src/**/*.ts", "SRC/Web/Api.TS", true));
}

TEST_CASE("glob_match: recursive globstar '**'") {
  CHECK(glob_match("**/*.min.js", "bundle.min.js"));
  CHECK(glob_match("**/*.min.js", "dist/bundle.min.js"));
  CHECK(glob_match("**/*.min.js", "assets/js/vendor/app.min.js"));
  CHECK_FALSE(glob_match("**/*.min.js", "bundle.js"));

  CHECK(glob_match(".git/**", ".git/config"));
  CHECK(glob_match(".git/**", ".git/objects/pack/pack-1.pack"));
  CHECK_FALSE(glob_match(".git/**", ".gitignore"));

  CHECK(glob_match("**/node_modules/**", "node_modules/pkg/index.js"));
  CHECK(glob_match("**/node_modules/**", "packages/web/node_modules/pkg/index.js"));
  CHECK(glob_match("**/node_modules/**", "node_modules"));
  CHECK_FALSE(glob_match("**/node_modules/**", "my_node_modules/index.js"));

  CHECK(glob_match("**", "any/arbitrary/nested/path.cpp"));
  CHECK(glob_match("**", "file.txt"));
  CHECK(glob_match("**", ""));
}

TEST_CASE("glob_match: intermediate '/**/'") {
  CHECK(glob_match("a/**/b", "a/b"));
  CHECK(glob_match("a/**/b", "a/x/b"));
  CHECK(glob_match("a/**/b", "a/x/y/z/b"));
  CHECK_FALSE(glob_match("a/**/b", "a/x/c"));
  CHECK_FALSE(glob_match("a/**/b", "c/x/b"));
}

TEST_CASE("glob_match: brace expansion '{a,b}'") {
  CHECK(glob_match("*.{py,ts}", "app.py"));
  CHECK(glob_match("*.{py,ts}", "app.ts"));
  CHECK_FALSE(glob_match("*.{py,ts}", "app.js"));

  CHECK(glob_match("src/{models,views}/*.py", "src/models/user.py"));
  CHECK(glob_match("src/{models,views}/*.py", "src/views/index.py"));
  CHECK_FALSE(glob_match("src/{models,views}/*.py", "src/controllers/api.py"));

  CHECK(glob_match("foo{bar}baz", "foo{bar}baz"));
  CHECK_FALSE(glob_match("foo{bar}baz", "foobarbaz"));
}

TEST_CASE("glob_match: nested and Cartesian braces") {
  CHECK(glob_match("{src,test}/*.{py,ts}", "src/main.py"));
  CHECK(glob_match("{src,test}/*.{py,ts}", "test/test_main.ts"));
  CHECK_FALSE(glob_match("{src,test}/*.{py,ts}", "docs/main.py"));
}

TEST_CASE("normalize_rel_path: slashes, redundant './', trailing slashes") {
  CHECK(normalize_rel_path("src\\core\\paths.cpp") == "src/core/paths.cpp");
  CHECK(normalize_rel_path("src///core//paths.cpp") == "src/core/paths.cpp");
  CHECK(normalize_rel_path("./src/core/paths.cpp") == "src/core/paths.cpp");
  CHECK(normalize_rel_path("src/./core/paths.cpp") == "src/core/paths.cpp");
  CHECK(normalize_rel_path("src/core/") == "src/core");
  CHECK(normalize_rel_path(".") == "");
  CHECK(normalize_rel_path("./") == "");
}

TEST_CASE("normalize_rel_path: traversal rejection '..'") {
  CHECK_THROWS_AS(normalize_rel_path("../outside.py"), Error);
  CHECK_THROWS_AS(normalize_rel_path("src/../../outside.py"), Error);
  CHECK_THROWS_AS(normalize_rel_path("src/.."), Error);
  CHECK_THROWS_AS(normalize_rel_path(".."), Error);
}

TEST_CASE("normalize_rel_path: absolute path rejection") {
  CHECK_THROWS_AS(normalize_rel_path("/etc/passwd"), Error);
  CHECK_THROWS_AS(normalize_rel_path("C:/Windows/System32"), Error);
  CHECK_THROWS_AS(normalize_rel_path("C:\\Windows"), Error);
  CHECK_THROWS_AS(normalize_rel_path("C:file.txt"), Error);
  CHECK_THROWS_AS(normalize_rel_path("//server/share/file"), Error);
}

TEST_CASE("is_safe_rel_path: Windows reserved device names") {
  CHECK_FALSE(is_safe_rel_path("con"));
  CHECK_FALSE(is_safe_rel_path("prn"));
  CHECK_FALSE(is_safe_rel_path("aux"));
  CHECK_FALSE(is_safe_rel_path("nul"));
  CHECK_FALSE(is_safe_rel_path("com1"));
  CHECK_FALSE(is_safe_rel_path("com9"));
  CHECK_FALSE(is_safe_rel_path("lpt1"));
  CHECK_FALSE(is_safe_rel_path("lpt9"));

  CHECK_FALSE(is_safe_rel_path("CON"));
  CHECK_FALSE(is_safe_rel_path("Nul"));
  CHECK_FALSE(is_safe_rel_path("AuX"));
  CHECK_FALSE(is_safe_rel_path("COM3"));
}

TEST_CASE("is_safe_rel_path: reserved names with extensions") {
  CHECK_FALSE(is_safe_rel_path("con.py"));
  CHECK_FALSE(is_safe_rel_path("aux.txt"));
  CHECK_FALSE(is_safe_rel_path("NUL.dat"));
  CHECK_FALSE(is_safe_rel_path("com1.json"));
  CHECK_FALSE(is_safe_rel_path("src/con.py"));
  CHECK_FALSE(is_safe_rel_path("pkg/aux/setup.py"));

  CHECK(is_safe_rel_path("contact.py"));
  CHECK(is_safe_rel_path("console.ts"));
  CHECK(is_safe_rel_path("auxiliary.cpp"));
  CHECK(is_safe_rel_path("com10.dat"));
}

TEST_CASE("is_safe_rel_path: trailing dot and space rejection") {
  CHECK_FALSE(is_safe_rel_path("foo."));
  CHECK_FALSE(is_safe_rel_path("foo "));
  CHECK_FALSE(is_safe_rel_path("src/dir. /file.py"));
}

TEST_CASE("is_safe_rel_path: illegal characters '<>:\"|?*'") {
  CHECK_FALSE(is_safe_rel_path("foo<bar>.py"));
  CHECK_FALSE(is_safe_rel_path("foo:bar.py"));
  CHECK_FALSE(is_safe_rel_path("foo\"bar.py"));
  CHECK_FALSE(is_safe_rel_path("foo|bar.py"));
  CHECK_FALSE(is_safe_rel_path("foo?bar.py"));
  CHECK_FALSE(is_safe_rel_path("foo*bar.py"));
  CHECK_FALSE(is_safe_rel_path("foo\001bar.py"));
}

TEST_CASE("to_workspace_rel & is_contained: containment & escaping") {
  fs::path root = fs::current_path();
  fs::path target = root / "src" / "core" / "paths.cpp";

  std::string rel = to_workspace_rel(root, target);
  CHECK(rel == "src/core/paths.cpp");
  CHECK(is_contained(root, rel));

  fs::path outside = root.parent_path() / "outside_file.txt";
  CHECK_THROWS_AS(to_workspace_rel(root, outside), Error);
  CHECK_FALSE(is_contained(root, "../outside_file.txt"));
}

} // TEST_SUITE("core::paths")

TEST_SUITE("core::config") {

TEST_CASE("Config: default values integrity") {
  Config cfg;
  CHECK(cfg.falkordb.host == "127.0.0.1");
  CHECK(cfg.falkordb.port == 6390);
  CHECK(cfg.falkordb.graph_prefix == "gs");
  CHECK(cfg.index.max_file_bytes == 1048576);
  CHECK(cfg.index.use_git_ls_files == true);
  CHECK(cfg.index.ignore.size() == 13);
  CHECK(cfg.python.source_roots == std::vector<std::string>{"", "src"});
  CHECK(cfg.gate.mode == "enforce");
  CHECK(cfg.gate.protected_patterns.size() == 10);
  CHECK(cfg.memory.user_graph == "gs:__user__");
  CHECK(cfg.log.level == "info");
}

TEST_CASE("Config: JSON loading and override merging") {
  fs::path tmp_dir = fs::temp_directory_path() / "gs_test_config_json";
  fs::remove_all(tmp_dir);
  fs::create_directories(tmp_dir / ".graphstate");

  std::ofstream out(tmp_dir / ".graphstate" / "config.json");
  out << R"({
    "falkordb": { "port": 6399, "host": "10.0.0.1" },
    "index": { "max_file_bytes": 2097152 },
    "gate": { "mode": "warn" }
  })";
  out.close();

  Config cfg = Config::load(tmp_dir);
  CHECK(cfg.falkordb.port == 6399);
  CHECK(cfg.falkordb.host == "10.0.0.1");
  CHECK(cfg.index.max_file_bytes == 2097152);
  CHECK(cfg.gate.mode == "warn");
  CHECK(cfg.falkordb.graph_prefix == "gs");
  CHECK(cfg.index.use_git_ls_files == true);

  fs::remove_all(tmp_dir);
}

TEST_CASE("Config: environment variable overrides") {
#ifdef _WIN32
  _putenv("GRAPHSTATE_FALKOR_PORT=6388");
  _putenv("GRAPHSTATE_LOG_LEVEL=debug");
  _putenv("GRAPHSTATE_GATE_MODE=off");
  _putenv("GRAPHSTATE_NO_WATCH=1");
#else
  setenv("GRAPHSTATE_FALKOR_PORT", "6388", 1);
  setenv("GRAPHSTATE_LOG_LEVEL", "debug", 1);
  setenv("GRAPHSTATE_GATE_MODE", "off", 1);
  setenv("GRAPHSTATE_NO_WATCH", "1", 1);
#endif

  Config cfg = Config::load(fs::current_path());
  CHECK(cfg.falkordb.port == 6388);
  CHECK(cfg.log.level == "debug");
  CHECK(cfg.gate.mode == "off");
  CHECK(cfg.watch.enabled == false);

#ifdef _WIN32
  _putenv("GRAPHSTATE_FALKOR_PORT=");
  _putenv("GRAPHSTATE_LOG_LEVEL=");
  _putenv("GRAPHSTATE_GATE_MODE=");
  _putenv("GRAPHSTATE_NO_WATCH=");
#else
  unsetenv("GRAPHSTATE_FALKOR_PORT");
  unsetenv("GRAPHSTATE_LOG_LEVEL");
  unsetenv("GRAPHSTATE_GATE_MODE");
  unsetenv("GRAPHSTATE_NO_WATCH");
#endif
}

TEST_CASE("Config: workspace key derivation and sanitization") {
  Config cfg;
  cfg.root = fs::path("g:/GraphState/tests/fixtures/benchmark_project");

  std::string key = cfg.workspace_key();
  CHECK(key.starts_with("benchmark_project-"));
  CHECK(key.size() == std::string("benchmark_project-").size() + 8);
  CHECK(cfg.project_graph() == "gs:" + key);
  CHECK(cfg.user_graph() == "gs:__user__");
}

TEST_CASE("Config: effective parse threads bounding") {
  Config cfg;
  cfg.index.parse_threads = 0;
  int eff = cfg.effective_parse_threads();
  CHECK(eff >= 1);
  CHECK(eff <= 8);

  cfg.index.parse_threads = 16;
  CHECK(cfg.effective_parse_threads() == 16);
}

} // TEST_SUITE("core::config")

TEST_SUITE("adversarial::glob") {

TEST_CASE("adversarial glob: nested brace expansion") {
  CHECK(glob_match("{a,{b,c}}", "a"));
  CHECK(glob_match("{a,{b,c}}", "b"));
  CHECK(glob_match("{a,{b,c}}", "c"));
  CHECK_FALSE(glob_match("{a,{b,c}}", "d"));

  CHECK(glob_match("{a,{b,{c,d}}}", "a"));
  CHECK(glob_match("{a,{b,{c,d}}}", "b"));
  CHECK(glob_match("{a,{b,{c,d}}}", "c"));
  CHECK(glob_match("{a,{b,{c,d}}}", "d"));
  CHECK_FALSE(glob_match("{a,{b,{c,d}}}", "e"));

  CHECK(glob_match("src/{components,utils/{helpers,formatters}}/*.ts", "src/components/button.ts"));
  CHECK(glob_match("src/{components,utils/{helpers,formatters}}/*.ts", "src/utils/helpers/dom.ts"));
  CHECK(glob_match("src/{components,utils/{helpers,formatters}}/*.ts", "src/utils/formatters/date.ts"));
  CHECK_FALSE(glob_match("src/{components,utils/{helpers,formatters}}/*.ts", "src/utils/other.ts"));
  CHECK_FALSE(glob_match("src/{components,utils/{helpers,formatters}}/*.ts", "src/models/user.ts"));

  // Cartesian sibling braces
  CHECK(glob_match("{a,b}{1,2}", "a1"));
  CHECK(glob_match("{a,b}{1,2}", "a2"));
  CHECK(glob_match("{a,b}{1,2}", "b1"));
  CHECK(glob_match("{a,b}{1,2}", "b2"));
  CHECK_FALSE(glob_match("{a,b}{1,2}", "a3"));
  CHECK_FALSE(glob_match("{a,b}{1,2}", "ab"));
}

TEST_CASE("adversarial glob: mismatched and malformed braces") {
  // Unclosed opening brace treated literally
  CHECK(glob_match("{a,b", "{a,b"));
  CHECK_FALSE(glob_match("{a,b", "a"));
  CHECK_FALSE(glob_match("{a,b", "b"));

  // Unopened closing brace treated literally
  CHECK(glob_match("a,b}", "a,b}"));
  CHECK_FALSE(glob_match("a,b}", "a"));

  // Empty braces
  CHECK(glob_match("{}", "{}"));
  CHECK_FALSE(glob_match("{}", ""));

  // Single item braces (no comma) treated literally
  CHECK(glob_match("{a}", "{a}"));
  CHECK_FALSE(glob_match("{a}", "a"));

  // Empty alternatives
  CHECK(glob_match("{,}", ""));
  CHECK(glob_match("foo{,bar}", "foo"));
  CHECK(glob_match("foo{,bar}", "foobar"));
  CHECK_FALSE(glob_match("foo{,bar}", "foobaz"));

  // Inverted braces
  CHECK(glob_match("}{a,b}{", "}a{"));
  CHECK(glob_match("}{a,b}{", "}b{"));
  CHECK_FALSE(glob_match("}{a,b}{", "a"));
}

TEST_CASE("adversarial glob: regex metacharacters treated as literals") {
  // '+' character
  CHECK(glob_match("file+(1).py", "file+(1).py"));
  CHECK_FALSE(glob_match("file+(1).py", "file+.py"));
  CHECK(glob_match("file+*.py", "file+(1).py"));

  // Parentheses '(', ')'
  CHECK(glob_match("build(v2)/*.js", "build(v2)/app.js"));
  CHECK_FALSE(glob_match("build(v2)/*.js", "buildv2/app.js"));

  // Anchors '^', '$'
  CHECK_FALSE(glob_match("^foo$", "foo"));
  CHECK(glob_match("^foo$", "^foo$"));
  CHECK(glob_match("*$*", "price$sale.txt"));
  CHECK(glob_match("*^*", "pow^2.dat"));

  // Brackets '[', ']'
  CHECK(glob_match("foo[1].cpp", "foo[1].cpp"));
  CHECK_FALSE(glob_match("foo[1-9].cpp", "foo2.cpp"));
}

TEST_CASE("adversarial glob: consecutive asterisks '***'") {
  // Triple asterisk in a single segment acts as single segment wildcard (matches any string with 1 segment, not empty/cross-segment)
  CHECK(glob_match("***", "foo"));
  CHECK_FALSE(glob_match("***", ""));
  CHECK_FALSE(glob_match("***", "foo/bar"));

  CHECK(glob_match("***/foo", "a/foo"));
  CHECK_FALSE(glob_match("***/foo", "a/b/foo"));

  CHECK(glob_match("foo/***", "foo/bar"));
  CHECK_FALSE(glob_match("foo/***", "foo/bar/baz"));

  // Multiple consecutive globstars
  CHECK(glob_match("**/**", "a/b/c"));
  CHECK(glob_match("**/**/**", "file.txt"));
  CHECK(glob_match("**/***/**", "a/b/c"));
  CHECK(glob_match("**/***/**", "foo"));
}

TEST_CASE("adversarial glob: intermediate '/**/' with root and leaf matches") {
  // Leading globstar
  CHECK(glob_match("/**/foo", "foo"));
  CHECK(glob_match("/**/foo", "a/foo"));
  CHECK(glob_match("/**/foo", "a/b/c/foo"));
  CHECK_FALSE(glob_match("/**/foo", "foo/bar"));

  // Trailing globstar
  CHECK(glob_match("foo/**/", "foo"));
  CHECK(glob_match("foo/**/", "foo/a"));
  CHECK(glob_match("foo/**/", "foo/a/b/c"));
  CHECK_FALSE(glob_match("foo/**/", "bar/foo"));

  // Just globstar with slashes
  CHECK(glob_match("/**/", "a/b/c"));
  CHECK(glob_match("/**/", "file.txt"));
  CHECK(glob_match("/**/", ""));

  // Complex multi-globstar sandwich
  CHECK(glob_match("/**/a/**/b/**/", "a/b"));
  CHECK(glob_match("/**/a/**/b/**/", "x/a/y/b/z"));
  CHECK_FALSE(glob_match("/**/a/**/b/**/", "x/a/y/c/z"));
  CHECK_FALSE(glob_match("/**/a/**/b/**/", "b/a"));
}

} // TEST_SUITE("adversarial::glob")

TEST_SUITE("adversarial::paths") {

TEST_CASE("adversarial paths: exhaustive Win32 reserved device names") {
  const std::vector<std::string> reserved_stems = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
  };

  const std::vector<std::string> extensions = {
    "", ".py", ".txt", ".json", ".tar.gz", ".cpp", ".h"
  };

  for (const auto& stem : reserved_stems) {
    for (const auto& ext : extensions) {
      std::string filename = stem + ext;
      INFO("Checking reserved filename: ", filename);
      CHECK(!is_safe_rel_path(filename));
      CHECK_THROWS_AS(normalize_rel_path(filename), Error);

      std::string nested = "src/" + filename;
      INFO("Checking nested reserved filename: ", nested);
      CHECK(!is_safe_rel_path(nested));
      CHECK_THROWS_AS(normalize_rel_path(nested), Error);

      // Mixed case
      std::string mixed = filename;
      for (size_t i = 0; i < mixed.size(); ++i) {
        if (i % 2 == 1 && std::isalpha(static_cast<unsigned char>(mixed[i]))) {
          mixed[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(mixed[i])));
        }
      }
      INFO("Checking mixed-case reserved filename: ", mixed);
      CHECK(!is_safe_rel_path(mixed));
      CHECK_THROWS_AS(normalize_rel_path(mixed), Error);
    }
  }

  // Reserved stems as directory segments
  CHECK_FALSE(is_safe_rel_path("prn/foo.py"));
  CHECK_THROWS_AS(normalize_rel_path("prn/foo.py"), Error);
  CHECK_FALSE(is_safe_rel_path("pkg/aux/bar.ts"));
  CHECK_THROWS_AS(normalize_rel_path("pkg/aux/bar.ts"), Error);
  CHECK_FALSE(is_safe_rel_path("con/test.py"));
  CHECK_THROWS_AS(normalize_rel_path("con/test.py"), Error);
}

TEST_CASE("adversarial paths: non-reserved lookalikes are allowed") {
  CHECK(is_safe_rel_path("contact.py"));
  CHECK(is_safe_rel_path("console.ts"));
  CHECK(is_safe_rel_path("auxiliary.cpp"));
  CHECK(is_safe_rel_path("com10.dat"));
  CHECK(is_safe_rel_path("lpt10.log"));
  CHECK(is_safe_rel_path("null.json"));
  CHECK(is_safe_rel_path("pron.py"));
  CHECK(is_safe_rel_path("prnt.doc"));

  CHECK(normalize_rel_path("contact.py") == "contact.py");
  CHECK(normalize_rel_path("src/console.ts") == "src/console.ts");
}

TEST_CASE("adversarial paths: traversal rejection in all positions") {
  CHECK_THROWS_AS(normalize_rel_path("foo/../bar"), Error);
  CHECK_THROWS_AS(normalize_rel_path("foo/bar/.."), Error);
  CHECK_THROWS_AS(normalize_rel_path("a/b/../../../etc/passwd"), Error);
  CHECK_THROWS_AS(normalize_rel_path("./../foo"), Error);
  CHECK_THROWS_AS(normalize_rel_path("foo/./../bar"), Error);
}

TEST_CASE("adversarial paths: absolute, UNC, and device namespace paths") {
  CHECK_THROWS_AS(normalize_rel_path("C:/foo"), Error);
  CHECK_THROWS_AS(normalize_rel_path("C:\\foo"), Error);
  CHECK_THROWS_AS(normalize_rel_path("D:/project/file.py"), Error);
  CHECK_THROWS_AS(normalize_rel_path("/etc/passwd"), Error);
  CHECK_THROWS_AS(normalize_rel_path("//server/share"), Error);
  CHECK_THROWS_AS(normalize_rel_path("\\\\server\\share"), Error);
  CHECK_THROWS_AS(normalize_rel_path("\\\\server\\share\\file.txt"), Error);
  CHECK_THROWS_AS(normalize_rel_path("\\\\?\\C:\\foo"), Error);
  CHECK_THROWS_AS(normalize_rel_path("//?/C:/foo"), Error);
  CHECK_THROWS_AS(normalize_rel_path("\\\\.\\COM1"), Error);
}

TEST_CASE("adversarial paths: containment verification") {
  fs::path root = fs::current_path();

  CHECK(is_contained(root, "src/core/paths.cpp"));
  CHECK(is_contained(root, "nonexistent/sub/file.cpp"));
  CHECK_FALSE(is_contained(root, "../outside.py"));
  CHECK_FALSE(is_contained(root, "foo/../../outside.py"));
  CHECK_FALSE(is_contained(root, "/outside.py"));
  CHECK_FALSE(is_contained(root, "C:/outside.py"));
  CHECK_FALSE(is_contained(root, "\\outside.py"));

  CHECK(to_workspace_rel(root, root / "src" / "core" / "paths.cpp") == "src/core/paths.cpp");
  CHECK(to_workspace_rel(root, root) == "");
  CHECK_THROWS_AS(to_workspace_rel(root, root.parent_path() / "outside.txt"), Error);
}

} // TEST_SUITE("adversarial::paths")

TEST_SUITE("adversarial::config") {

TEST_CASE("adversarial config: malformed JSON tolerance") {
  fs::path tmp_dir = fs::temp_directory_path() / "gs_test_adv_config";
  fs::remove_all(tmp_dir);
  fs::create_directories(tmp_dir / ".graphstate");

  auto test_malformed = [&](const std::string& content) {
    std::ofstream out(tmp_dir / ".graphstate" / "config.json");
    out << content;
    out.close();

    Config cfg = Config::load(tmp_dir);
    // Should gracefully fall back to defaults without crashing or throwing
    CHECK(cfg.falkordb.port == 6390);
    CHECK(cfg.falkordb.host == "127.0.0.1");
    CHECK(cfg.gate.mode == "enforce");
  };

  test_malformed("");                     // Empty file
  test_malformed("{ invalid json ");      // Truncated / syntax error
  test_malformed("{{{");                  // Invalid syntax
  test_malformed("[1, 2, 3]");            // JSON array at root
  test_malformed("\"hello\"");            // JSON string at root
  test_malformed("12345");                // JSON number at root
  test_malformed("{\"gate\": {\"mode\": \"warn\",}}"); // Trailing comma

  // Invalid types within objects
  test_malformed(R"({
    "falkordb": { "port": "not-a-number", "host": 12345 },
    "index": { "max_file_bytes": "1MB", "ignore": "not-array" },
    "gate": { "mode": 999 }
  })");

  fs::remove_all(tmp_dir);
}

TEST_CASE("adversarial config: environment variable overrides robustness") {
#ifdef _WIN32
  _putenv("GRAPHSTATE_FALKOR_PORT=not_a_number");
  _putenv("GRAPHSTATE_FALKOR_HOST=test.db.local");
  _putenv("GRAPHSTATE_LOG_LEVEL=warn");
  _putenv("GRAPHSTATE_GATE_MODE=audit");
  _putenv("GRAPHSTATE_NO_WATCH=true");
#else
  setenv("GRAPHSTATE_FALKOR_PORT", "not_a_number", 1);
  setenv("GRAPHSTATE_FALKOR_HOST", "test.db.local", 1);
  setenv("GRAPHSTATE_LOG_LEVEL", "warn", 1);
  setenv("GRAPHSTATE_GATE_MODE", "audit", 1);
  setenv("GRAPHSTATE_NO_WATCH", "true", 1);
#endif

  Config cfg = Config::load(fs::current_path());
  // Non-numeric port should be safely ignored and retain default
  CHECK(cfg.falkordb.port == 6390);
  CHECK(cfg.falkordb.host == "test.db.local");
  CHECK(cfg.log.level == "warn");
  CHECK(cfg.gate.mode == "audit");
  CHECK(cfg.watch.enabled == false);

#ifdef _WIN32
  _putenv("GRAPHSTATE_NO_WATCH=0");
#else
  setenv("GRAPHSTATE_NO_WATCH", "0", 1);
#endif
  Config cfg2 = Config::load(fs::current_path());
  CHECK(cfg2.watch.enabled == true);

#ifdef _WIN32
  _putenv("GRAPHSTATE_FALKOR_PORT=");
  _putenv("GRAPHSTATE_FALKOR_HOST=");
  _putenv("GRAPHSTATE_LOG_LEVEL=");
  _putenv("GRAPHSTATE_GATE_MODE=");
  _putenv("GRAPHSTATE_NO_WATCH=");
#else
  unsetenv("GRAPHSTATE_FALKOR_PORT");
  unsetenv("GRAPHSTATE_FALKOR_HOST");
  unsetenv("GRAPHSTATE_LOG_LEVEL");
  unsetenv("GRAPHSTATE_GATE_MODE");
  unsetenv("GRAPHSTATE_NO_WATCH");
#endif
}

TEST_CASE("adversarial config: workspace key edge cases") {
  Config cfg;

  // Trailing slash invariance
  cfg.root = fs::path("g:/GraphState/");
  std::string key1 = cfg.workspace_key();
  cfg.root = fs::path("g:/GraphState");
  std::string key2 = cfg.workspace_key();
  CHECK(key1 == key2);

  // Root directory
  cfg.root = fs::path("C:/");
  std::string key_root = cfg.workspace_key();
  CHECK(key_root.starts_with("workspace-"));

  // Non-alphanumeric and length truncation (> 32 chars)
  cfg.root = fs::path("g:/GraphState/this_is_a_very_long_workspace_directory_name_that_exceeds_32_characters");
  std::string key_long = cfg.workspace_key();
  CHECK(key_long.starts_with("this_is_a_very_long_workspace_di-"));
}

TEST_CASE("adversarial config: design spec schema key 'protected'") {
  fs::path tmp_dir = fs::temp_directory_path() / "gs_test_spec_protected";
  fs::remove_all(tmp_dir);
  fs::create_directories(tmp_dir / ".graphstate");

  // Per docs/design/03-repo-and-build.md §3.5, the key is "protected", not "protected_patterns"
  std::ofstream out(tmp_dir / ".graphstate" / "config.json");
  out << R"({
    "gate": {
      "protected": ["secret/**", "private/**"]
    }
  })";
  out.close();

  Config cfg = Config::load(tmp_dir);
  // Check if "protected" from design doc is recognized or ignored
  bool spec_key_parsed = (cfg.gate.protected_patterns.size() == 2 &&
                          cfg.gate.protected_patterns[0] == "secret/**");
  // Check if code-only key "protected_patterns" works
  std::ofstream out2(tmp_dir / ".graphstate" / "config.json");
  out2 << R"({
    "gate": {
      "protected_patterns": ["secret/**", "private/**"]
    }
  })";
  out2.close();

  Config cfg2 = Config::load(tmp_dir);
  bool code_key_parsed = (cfg2.gate.protected_patterns.size() == 2 &&
                          cfg2.gate.protected_patterns[0] == "secret/**");

  CHECK(code_key_parsed);
  // We document whether spec_key_parsed succeeds or fails
  WARN_MESSAGE(spec_key_parsed, "Design spec §3.5 key 'protected' was ignored; only 'protected_patterns' is accepted");

  fs::remove_all(tmp_dir);
}

TEST_CASE("adversarial config: GRAPHSTATE_ROOT precedence") {
  fs::path tmp_root = fs::temp_directory_path() / "gs_adv_root_override";
  fs::create_directories(tmp_root);

#ifdef _WIN32
  _putenv(("GRAPHSTATE_ROOT=" + tmp_root.string()).c_str());
#else
  setenv("GRAPHSTATE_ROOT", tmp_root.string().c_str(), 1);
#endif

  // 1. Empty root passed -> GRAPHSTATE_ROOT overrides it
  Config cfg_empty = Config::load({});
  CHECK(cfg_empty.root == tmp_root);

  // 2. Explicit root passed -> does GRAPHSTATE_ROOT override or does explicit root win?
  fs::path explicit_root = fs::current_path();
  Config cfg_explicit = Config::load(explicit_root);
  // Implementation line 197: only overrides if cfg.root.empty()
  CHECK(cfg_explicit.root == explicit_root);

#ifdef _WIN32
  _putenv("GRAPHSTATE_ROOT=");
#else
  unsetenv("GRAPHSTATE_ROOT");
#endif
  fs::remove_all(tmp_root);
}

} // TEST_SUITE("adversarial::config")

TEST_SUITE("adversarial::stress") {

TEST_CASE("adversarial stress: pathological glob backtracking and depth") {
  // Pathological wildcard pattern (classic ReDoS/backtracking target)
  // In our 2-pointer DP, this must be O(N*M) and finish in < 5 milliseconds
  std::string pat = "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b";
  std::string text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac";
  CHECK_FALSE(glob_match(pat, text));

  // Deeply nested braces (10 levels)
  std::string deep = "{a,{b,{c,{d,{e,{f,{g,{h,{i,j}}}}}}}}}";
  CHECK(glob_match(deep, "a"));
  CHECK(glob_match(deep, "e"));
  CHECK(glob_match(deep, "j"));
  CHECK_FALSE(glob_match(deep, "k"));

  // 20 consecutive globstars
  std::string many_stars = "**/**/**/**/**/**/**/**/**/**/**/**/**/**/**/**/**/**/**/**/target.py";
  CHECK(glob_match(many_stars, "a/b/c/d/e/target.py"));
  CHECK(glob_match(many_stars, "target.py"));
  CHECK_FALSE(glob_match(many_stars, "a/b/other.py"));
}

TEST_CASE("adversarial stress: Win32 edge reserved names COM0, CONIN$, CLOCK$") {
  // Test whether COM0 and LPT0 are rejected or permitted
  bool com0_safe = is_safe_rel_path("com0.py");
  bool lpt0_safe = is_safe_rel_path("lpt0.txt");
  bool conin_safe = is_safe_rel_path("conin$.txt");
  bool clock_safe = is_safe_rel_path("clock$.dat");

  // On standard DOS/Win32, COM1-COM9 and LPT1-LPT9 are the universal device names.
  // We document whether COM0/CONIN$/CLOCK$ are rejected by RESERVED_STEMS.
  WARN_MESSAGE(!com0_safe, "COM0 is not in RESERVED_STEMS (treated as safe: ", com0_safe, ")");
  WARN_MESSAGE(!lpt0_safe, "LPT0 is not in RESERVED_STEMS (treated as safe: ", lpt0_safe, ")");
  WARN_MESSAGE(!conin_safe, "CONIN$ is not in RESERVED_STEMS (treated as safe: ", conin_safe, ")");
  WARN_MESSAGE(!clock_safe, "CLOCK$ is not in RESERVED_STEMS (treated as safe: ", clock_safe, ")");
}

TEST_CASE("adversarial stress: containment with backslash-prefixed paths") {
  fs::path root = fs::current_path();

  // Root-relative backslash path: on Windows "root / \outside" resolves to drive root!
  // is_contained must reject this!
  CHECK_FALSE(is_contained(root, "\\outside.py"));
  CHECK_FALSE(is_contained(root, "\\Windows\\System32\\notepad.exe"));
}

} // TEST_SUITE("adversarial::stress")
