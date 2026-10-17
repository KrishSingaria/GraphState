#ifdef _WIN32

#include "platform/platform.hpp"

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <psapi.h>

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace gs::platform {

namespace {

std::function<void()> g_shutdown_cb;

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
  switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      if (g_shutdown_cb) {
        g_shutdown_cb();
      }
      return TRUE;
    default:
      return FALSE;
  }
}

std::wstring to_wstring(std::string_view s) {
  if (s.empty()) return L"";
  int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (size <= 0) return L"";
  std::wstring res(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &res[0], size);
  return res;
}

std::string from_wstring(std::wstring_view ws) {
  if (ws.empty()) return "";
  int size = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return "";
  std::string res(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), &res[0], size, nullptr, nullptr);
  return res;
}

std::wstring quote_arg_windows(const std::string& arg) {
  if (arg.empty()) {
    return L"\"\"";
  }
  std::wstring warg = to_wstring(arg);
  if (warg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return warg;
  }
  std::wstring res = L"\"";
  for (size_t i = 0; i < warg.size(); ++i) {
    size_t backslashes = 0;
    while (i < warg.size() && warg[i] == L'\\') {
      ++backslashes;
      ++i;
    }
    if (i == warg.size()) {
      res.append(backslashes * 2, L'\\');
      break;
    } else if (warg[i] == L'"') {
      res.append(backslashes * 2 + 1, L'\\');
      res.push_back(L'"');
    } else {
      res.append(backslashes, L'\\');
      res.push_back(warg[i]);
    }
  }
  res.push_back(L'"');
  return res;
}

} // namespace

void init_stdio() {
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
}

void on_shutdown_request(std::function<void()> cb) {
  g_shutdown_cb = std::move(cb);
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
}

[[noreturn]] void hard_exit(int code) {
  TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
  _exit(code);
}

std::string to_utf8(const fs::path& p) {
  const auto u8str = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8str.data()), u8str.size());
}

fs::path from_utf8(std::string_view s) {
  std::u8string u8(reinterpret_cast<const char8_t*>(s.data()), s.size());
  return fs::path(u8);
}

fs::path executable_path() {
  std::vector<wchar_t> buf(MAX_PATH);
  while (true) {
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) {
      return {};
    }
    if (len < buf.size()) {
      return fs::path(std::wstring(buf.data(), len));
    }
    buf.resize(buf.size() * 2);
  }
}

bool fs_case_insensitive(const fs::path& dir) {
  try {
    fs::path p1 = dir / "gs_CaseProbe";
    fs::path p2 = dir / "gs_caseprobe";
    std::ofstream out(p1);
    if (!out.is_open()) return true;
    out << "probe";
    out.close();

    bool ci = fs::exists(p2);
    std::error_code ec;
    fs::remove(p1, ec);
    return ci;
  } catch (...) {
    return true; // default to case-insensitive on Windows
  }
}

FileStat stat(const fs::path& p) {
  FileStat res;
  std::error_code ec;
  auto status = fs::status(p, ec);
  if (ec || !fs::exists(status)) {
    res.exists = false;
    return res;
  }
  res.exists = true;
  res.regular = fs::is_regular_file(status);
  if (res.regular) {
    res.size = fs::file_size(p, ec);
    if (ec) res.size = 0;
  }

  auto lwt = fs::last_write_time(p, ec);
  if (!ec) {
    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(lwt);
    auto duration = sctp.time_since_epoch();
    res.mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  }
  return res;
}

void write_file_durable(const fs::path& p, std::string_view bytes, bool sync) {
  std::wstring wp = p.wstring();
  HANDLE h = CreateFileW(
      wp.c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
  );
  if (h == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    throw Error(ErrorCode::WRITE_FAILED, "failed to open file for durable write: " + to_utf8(p), {{"winerr", static_cast<int>(err)}});
  }

  DWORD written = 0;
  const char* ptr = bytes.data();
  size_t remaining = bytes.size();
  while (remaining > 0) {
    DWORD chunk = static_cast<DWORD>((remaining > 1024 * 1024) ? 1024 * 1024 : remaining);
    if (!WriteFile(h, ptr, chunk, &written, nullptr) || written == 0) {
      DWORD err = GetLastError();
      CloseHandle(h);
      throw Error(ErrorCode::WRITE_FAILED, "failed to write file: " + to_utf8(p), {{"winerr", static_cast<int>(err)}});
    }
    ptr += written;
    remaining -= written;
  }

  if (sync) {
    if (!FlushFileBuffers(h)) {
      DWORD err = GetLastError();
      CloseHandle(h);
      throw Error(ErrorCode::WRITE_FAILED, "failed to flush file: " + to_utf8(p), {{"winerr", static_cast<int>(err)}});
    }
  }
  CloseHandle(h);
}

void replace_file(const fs::path& src_tmp, const fs::path& dst, bool write_through) {
  std::wstring wsrc = src_tmp.wstring();
  std::wstring wdst = dst.wstring();

  DWORD flags = MOVEFILE_REPLACE_EXISTING;
  if (write_through) {
    flags |= MOVEFILE_WRITE_THROUGH;
  }

  DWORD last_err = 0;
  for (int retry = 0; retry < 20; ++retry) {
    if (MoveFileExW(wsrc.c_str(), wdst.c_str(), flags)) {
      return;
    }
    last_err = GetLastError();
    if (last_err != ERROR_SHARING_VIOLATION &&
        last_err != ERROR_ACCESS_DENIED &&
        last_err != ERROR_LOCK_VIOLATION) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  throw Error(ErrorCode::WRITE_FAILED, "file is locked by another process", {
    {"path", to_utf8(dst)},
    {"winerr", static_cast<int>(last_err)}
  });
}

void remove_file(const fs::path& p) {
  std::wstring wp = p.wstring();
  DWORD last_err = 0;
  for (int retry = 0; retry < 20; ++retry) {
    if (DeleteFileW(wp.c_str())) {
      return;
    }
    last_err = GetLastError();
    if (last_err == ERROR_FILE_NOT_FOUND) {
      return;
    }
    if (last_err != ERROR_SHARING_VIOLATION &&
        last_err != ERROR_ACCESS_DENIED &&
        last_err != ERROR_LOCK_VIOLATION) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }

  throw Error(ErrorCode::WRITE_FAILED, "failed to delete file: " + to_utf8(p), {
    {"path", to_utf8(p)},
    {"winerr", static_cast<int>(last_err)}
  });
}

void fsync_dir(const fs::path&) {
  // NTFS journals directory metadata automatically
}

bool hardlink_or_copy(const fs::path& from, const fs::path& to) {
  std::wstring wfrom = from.wstring();
  std::wstring wto = to.wstring();
  if (CreateHardLinkW(wto.c_str(), wfrom.c_str(), nullptr)) {
    return true;
  }
  std::error_code ec;
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  return false;
}

FileMode file_mode(const fs::path& p) {
  FileMode mode;
  DWORD attrs = GetFileAttributesW(p.wstring().c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    mode.readonly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
  }
  return mode;
}

void set_file_mode(const fs::path& p, const FileMode& mode) {
  std::wstring wp = p.wstring();
  DWORD attrs = GetFileAttributesW(wp.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    if (mode.readonly) {
      attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
      attrs &= ~FILE_ATTRIBUTE_READONLY;
    }
    SetFileAttributesW(wp.c_str(), attrs);
  }
}

ProcResult run_capture(const std::vector<std::string>& argv, const fs::path& cwd, int timeout_ms) {
  ProcResult result;
  if (argv.empty()) {
    result.exit_code = -1;
    return result;
  }

  std::wstring cmdline;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i > 0) cmdline += L" ";
    cmdline += quote_arg_windows(argv[i]);
  }

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE out_read = nullptr, out_write = nullptr;
  HANDLE err_read = nullptr, err_write = nullptr;

  if (!CreatePipe(&out_read, &out_write, &sa, 0) || !CreatePipe(&err_read, &err_write, &sa, 0)) {
    if (out_read) CloseHandle(out_read);
    if (out_write) CloseHandle(out_write);
    if (err_read) CloseHandle(err_read);
    if (err_write) CloseHandle(err_write);
    result.exit_code = -1;
    return result;
  }

  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdOutput = out_write;
  si.hStdError = err_write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.dwFlags |= STARTF_USESTDHANDLES;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  std::wstring wcwd = cwd.empty() ? L"" : cwd.wstring();
  LPCWSTR pcwd = wcwd.empty() ? nullptr : wcwd.c_str();

  std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
  cmd_buf.push_back(L'\0');

  BOOL ok = CreateProcessW(
      nullptr,
      cmd_buf.data(),
      nullptr,
      nullptr,
      TRUE,
      0,
      nullptr,
      pcwd,
      &si,
      &pi
  );

  CloseHandle(out_write);
  CloseHandle(err_write);

  if (!ok) {
    CloseHandle(out_read);
    CloseHandle(err_read);
    result.exit_code = -1;
    return result;
  }

  auto read_pipe = [](HANDLE h, std::string& out) {
    char buf[4096];
    DWORD bytes = 0;
    while (ReadFile(h, buf, sizeof(buf), &bytes, nullptr) && bytes > 0) {
      out.append(buf, bytes);
    }
  };

  std::thread t_out([&] { read_pipe(out_read, result.out); });
  std::thread t_err([&] { read_pipe(err_read, result.err); });

  DWORD wait_res = WaitForSingleObject(pi.hProcess, timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : INFINITE);
  if (wait_res == WAIT_TIMEOUT) {
    result.timed_out = true;
    TerminateProcess(pi.hProcess, 1);
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  result.exit_code = static_cast<int>(exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  CloseHandle(out_read);
  CloseHandle(err_read);

  if (t_out.joinable()) t_out.join();
  if (t_err.joinable()) t_err.join();

  return result;
}

uint64_t peak_rss_bytes() {
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return pmc.PeakWorkingSetSize;
  }
  return 0;
}

std::string os_name() {
  return "windows";
}

} // namespace gs::platform

#endif // _WIN32
