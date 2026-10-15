#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "platform/platform.hpp"

TEST_CASE("platform::os_name returns valid platform") {
  std::string os = gs::platform::os_name();
  CHECK((os == "windows" || os == "linux" || os == "macos"));
}

TEST_CASE("platform::to_utf8 and from_utf8 roundtrip") {
  std::string original = "src/test_path/file.py";
  auto p = gs::platform::from_utf8(original);
  std::string roundtrip = gs::platform::to_utf8(p);
  // normalize slashes for check
  for (char& c : roundtrip) {
    if (c == '\\') c = '/';
  }
  CHECK(roundtrip == original);
}
