#pragma once

#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace gs::mcp {

constexpr size_t MAX_LINE_BYTES = 16 * 1024 * 1024; // 16 MiB

class Transport {
public:
  virtual ~Transport() = default;

  // Reads the next message line from the input stream.
  // Returns std::nullopt on EOF.
  // Skips blank lines. If line exceeds MAX_LINE_BYTES, writes a -32600 error and continues.
  virtual std::optional<std::string> read_line() = 0;

  // Sends a JSON message out, newline-terminated and flushed.
  virtual void send_json(const nlohmann::json& msg) = 0;
};

class StdioTransport : public Transport {
public:
  explicit StdioTransport(std::istream& in = std::cin, std::ostream& out = std::cout);

  std::optional<std::string> read_line() override;
  void send_json(const nlohmann::json& msg) override;

private:
  std::istream& in_;
  std::ostream& out_;
  std::mutex out_mu_;
};

} // namespace gs::mcp
