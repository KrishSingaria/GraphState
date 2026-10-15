#pragma once

#include <exception>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace gs {

enum class ErrorCode {
  OK = 0,
  INTERNAL = 1,
  INVALID_ARGUMENT = 2,
  DB_UNAVAILABLE = 3,
  WRITE_FAILED = 4,
  PATH_OUTSIDE_WORKSPACE = 5,
  PATH_INVALID = 6,
  FILE_EXISTS = 7,
  FILE_NOT_FOUND = 8,
  NOT_IMPLEMENTED = 9,
  TIMEOUT = 10,
};

inline std::string_view error_code_name(ErrorCode code) {
  switch (code) {
    case ErrorCode::OK: return "OK";
    case ErrorCode::INTERNAL: return "INTERNAL";
    case ErrorCode::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case ErrorCode::DB_UNAVAILABLE: return "DB_UNAVAILABLE";
    case ErrorCode::WRITE_FAILED: return "WRITE_FAILED";
    case ErrorCode::PATH_OUTSIDE_WORKSPACE: return "PATH_OUTSIDE_WORKSPACE";
    case ErrorCode::PATH_INVALID: return "PATH_INVALID";
    case ErrorCode::FILE_EXISTS: return "FILE_EXISTS";
    case ErrorCode::FILE_NOT_FOUND: return "FILE_NOT_FOUND";
    case ErrorCode::NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
    case ErrorCode::TIMEOUT: return "TIMEOUT";
  }
  return "UNKNOWN";
}

class Error : public std::exception {
public:
  ErrorCode code;
  std::string message;
  nlohmann::json data;

  Error(ErrorCode c, std::string msg, nlohmann::json d = nlohmann::json::object())
      : code(c), message(std::move(msg)), data(std::move(d)) {}

  const char* what() const noexcept override {
    return message.c_str();
  }
};

} // namespace gs
