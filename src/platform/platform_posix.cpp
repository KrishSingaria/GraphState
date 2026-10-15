#ifndef _WIN32

#include "platform/platform.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace gs::platform {

namespace {

std::function<void()> g_shutdown_cb;
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int) {
  g_shutdown_requested = true;
  if (g_shutdown_cb) {
    g_shutdown_cb();
  }
}

} // namespace

void init_stdio() {
  // POSIX stdio is binary by default
}

void on_shutdown_request(std::function<void()> cb) {
  g_shutdown_cb = std::move(cb);
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

[[noreturn]] void hard_exit(int code) {
  _exit(code);
}

std::string to_utf8(const fs::path& p) {
  return p.u8string();
}

fs::path from_utf8(std::string_view s) {
  return fs::u8path(s);
}

fs::path executable_path() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buf(size);
  if (_NSGetExecutablePath(buf.data(), &size) == 0) {
    char real[PATH_MAX];
    if (realpath(buf.data(), real)) {
      return fs::path(real);
    }
  }
  return {};
#else
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len != -1) {
    buf[len] = '\0';
    return fs::path(buf);
  }
  return {};
#endif
}

bool fs_case_insensitive(const fs::path& dir) {
  try {
    fs::path p1 = dir / "gs_CaseProbe";
    fs::path p2 = dir / "gs_caseprobe";
    std::ofstream out(p1);
    if (!out.is_open()) return false;
    out << "probe";
    out.close();

    bool ci = fs::exists(p2);
    std::error_code ec;
    fs::remove(p1, ec);
    return ci;
  } catch (...) {
    return false;
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

void write_file_durable(const fs::path& p, std::string_view bytes) {
  int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    throw Error(ErrorCode::WRITE_FAILED, "failed to open file for durable write: " + to_utf8(p), {{"errno", errno}});
  }
  const char* ptr = bytes.data();
  size_t remaining = bytes.size();
  while (remaining > 0) {
    ssize_t n = write(fd, ptr, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      throw Error(ErrorCode::WRITE_FAILED, "failed to write file: " + to_utf8(p), {{"errno", errno}});
    }
    ptr += n;
    remaining -= n;
  }
  if (fsync(fd) != 0) {
    close(fd);
    throw Error(ErrorCode::WRITE_FAILED, "failed to fsync file: " + to_utf8(p), {{"errno", errno}});
  }
  close(fd);
}

void replace_file(const fs::path& src_tmp, const fs::path& dst) {
  if (rename(src_tmp.c_str(), dst.c_str()) != 0) {
    throw Error(ErrorCode::WRITE_FAILED, "failed to replace file: " + to_utf8(dst), {{"errno", errno}});
  }
}

void remove_file(const fs::path& p) {
  if (unlink(p.c_str()) != 0 && errno != ENOENT) {
    throw Error(ErrorCode::WRITE_FAILED, "failed to delete file: " + to_utf8(p), {{"errno", errno}});
  }
}

void fsync_dir(const fs::path& dir) {
  int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    fsync(fd);
    close(fd);
  }
}

bool hardlink_or_copy(const fs::path& from, const fs::path& to) {
  if (link(from.c_str(), to.c_str()) == 0) {
    return true;
  }
  std::error_code ec;
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  return false;
}

FileMode file_mode(const fs::path& p) {
  FileMode mode;
  struct stat st;
  if (::stat(p.c_str(), &st) == 0) {
    mode.posix_mode = st.st_mode & 07777;
    mode.readonly = (access(p.c_str(), W_OK) != 0);
  }
  return mode;
}

void set_file_mode(const fs::path& p, const FileMode& mode) {
  chmod(p.c_str(), static_cast<mode_t>(mode.posix_mode));
}

ProcResult run_capture(const std::vector<std::string>& argv, const fs::path& cwd, int timeout_ms) {
  ProcResult result;
  if (argv.empty()) {
    result.exit_code = -1;
    return result;
  }

  int out_pipe[2];
  int err_pipe[2];
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    result.exit_code = -1;
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(out_pipe[0]); close(out_pipe[1]);
    close(err_pipe[0]); close(err_pipe[1]);
    result.exit_code = -1;
    return result;
  }

  if (pid == 0) {
    // Child
    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) {
        _exit(127);
      }
    }
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[0]); close(out_pipe[1]);
    close(err_pipe[0]); close(err_pipe[1]);

    std::vector<char*> args;
    for (const auto& a : argv) {
      args.push_back(const_cast<char*>(a.c_str()));
    }
    args.push_back(nullptr);

    execvp(args[0], args.data());
    _exit(127);
  }

  // Parent
  close(out_pipe[1]);
  close(err_pipe[1]);

  auto read_all = [](int fd, std::string& out) {
    char buf[4096];
    ssize_t n = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
      out.append(buf, n);
    }
    close(fd);
  };

  std::thread t_out([&] { read_all(out_pipe[0], result.out); });
  std::thread t_err([&] { read_all(err_pipe[0], result.err); });

  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }

  if (t_out.joinable()) t_out.join();
  if (t_err.joinable()) t_err.join();

  return result;
}

uint64_t peak_rss_bytes() {
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
    return ru.ru_maxrss;
#else
    return static_cast<uint64_t>(ru.ru_maxrss) * 1024;
#endif
  }
  return 0;
}

std::string os_name() {
#if defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

} // namespace gs::platform

#endif // !_WIN32
