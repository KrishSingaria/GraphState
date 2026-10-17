#include "mcp/transport.hpp"

#include <algorithm>
#include <cctype>
#include "platform/platform.hpp"

namespace gs::mcp {

StdioTransport::StdioTransport(std::istream& in, std::ostream& out) : in_(in), out_(out) {
  if (&in_ == &std::cin) {
    platform::init_stdio();
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
  }
}

std::optional<std::string> StdioTransport::read_line() {
  std::string line;
  while (std::getline(in_, line)) {
    if (line.size() > MAX_LINE_BYTES) {
      send_json({
          {"jsonrpc", "2.0"},
          {"id", nullptr},
          {"error", {{"code", -32600}, {"message", "Request exceeds maximum size of 16 MiB"}}}
      });
      continue;
    }

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Skip empty lines
    bool all_ws = std::all_of(line.begin(), line.end(), [](unsigned char c) {
      return std::isspace(c);
    });
    if (all_ws) {
      continue;
    }

    return line;
  }

  return std::nullopt; // EOF
}

void StdioTransport::send_json(const nlohmann::json& msg) {
  std::lock_guard<std::mutex> lock(out_mu_);
  out_ << msg.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n" << std::flush;
}

} // namespace gs::mcp
