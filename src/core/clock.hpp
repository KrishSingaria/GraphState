#pragma once

#include <chrono>
#include <cstdint>

namespace gs::core {

inline int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

class Stopwatch {
public:
  Stopwatch() : start_(std::chrono::steady_clock::now()) {}

  double elapsed_ms() const {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now() - start_).count();
  }

  void reset() {
    start_ = std::chrono::steady_clock::now();
  }

private:
  std::chrono::steady_clock::time_point start_;
};

} // namespace gs::core
