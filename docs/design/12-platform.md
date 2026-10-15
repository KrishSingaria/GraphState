# 12 — Cross-Platform: Windows, macOS, Linux

> Index: [README.md](README.md). Code: `src/platform/*`, `CMakePresets.json`, `.github/workflows/ci.yml`.
> **Primary development platform is Windows.** Every milestone's acceptance check MUST pass on Windows first; macOS and Linux are verified by CI on every push.

## 12.1 Support matrix

| OS | Arch | Toolchain (minimum) | FalkorDB | Status |
|---|---|---|---|---|
| Windows 10 22H2 / 11 | x64 | Visual Studio 2022 17.10+ (MSVC 19.40+) **or** clang-cl 17+; CMake ≥ 3.24 + Ninja (both ship with VS) | Docker Desktop, WSL2 backend, **Linux containers** | primary dev |
| macOS 13+ | arm64, x64 | AppleClang 15+ (Xcode CLT), CMake ≥ 3.24, Ninja | Docker Desktop / OrbStack / Colima | CI + demo |
| Linux (Ubuntu 22.04+, Fedora 39+) | x64, arm64 | GCC 12+ or Clang 15+, CMake ≥ 3.24, Ninja | Docker Engine + compose plugin | CI |

Not supported: 32-bit, Windows on ARM (untested), MinGW (use MSVC/clang-cl).

## 12.2 Windows developer setup (exact steps)

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait"
winget install Git.Git Kitware.CMake Ninja-build.Ninja Docker.DockerDesktop Python.Python.3.12
# Enable long paths (admin, once):
reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f
# Build (from "x64 Native Tools Command Prompt for VS 2022" or after running vcvars64.bat):
docker compose up -d
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release -L unit
.\build\windows-msvc-release\graphstate.exe doctor
```
Optional: add `.graphstate\` and the build dir to Windows Defender exclusions (`graphstate doctor` prints the command) — real-time scanning holds file handles and slows renames.

## 12.3 `CMakePresets.json`

```json
{
  "version": 6,
  "configurePresets": [
    { "name": "base", "hidden": true, "generator": "Ninja", "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON" } },
    { "name": "windows-msvc-debug", "inherits": "base", "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" },
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "CMAKE_C_COMPILER": "cl", "CMAKE_CXX_COMPILER": "cl", "GS_WITH_CPP_GRAMMAR": "OFF" } },
    { "name": "windows-msvc-release", "inherits": "base", "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" },
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release", "CMAKE_C_COMPILER": "cl", "CMAKE_CXX_COMPILER": "cl" } },
    { "name": "windows-clangcl-release", "inherits": "windows-msvc-release",
      "cacheVariables": { "CMAKE_C_COMPILER": "clang-cl", "CMAKE_CXX_COMPILER": "clang-cl" } },
    { "name": "linux-release", "inherits": "base", "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" },
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "macos-release", "inherits": "base", "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Darwin" },
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "dev-asan", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "GS_SANITIZE": "ON", "GS_WITH_CPP_GRAMMAR": "OFF" } }
  ],
  "buildPresets": [
    { "name": "windows-msvc-debug", "configurePreset": "windows-msvc-debug" },
    { "name": "windows-msvc-release", "configurePreset": "windows-msvc-release" },
    { "name": "linux-release", "configurePreset": "linux-release" },
    { "name": "macos-release", "configurePreset": "macos-release" }
  ],
  "testPresets": [
    { "name": "windows-msvc-release", "configurePreset": "windows-msvc-release", "output": { "outputOnFailure": true } },
    { "name": "linux-release", "configurePreset": "linux-release", "output": { "outputOnFailure": true } },
    { "name": "macos-release", "configurePreset": "macos-release", "output": { "outputOnFailure": true } }
  ]
}
```

## 12.4 Compiler & linker flags (replaces the GCC-only flags in §03.4)

```cmake
# cmake/Flags.cmake — included by CMakeLists.txt; applied to graphstate_core, graphstate, tests
function(gs_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

# static CRT on Windows → single self-contained graphstate.exe (no VC++ redistributable needed)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```
- Vendored C (tree-sitter core + grammars): MSVC `/W0 /O2 /bigobj /utf-8` (grammar `parser.c` files are huge); GCC/Clang `-O2 -w`. Set in `Grammars.cmake` with `if(MSVC)`.
- Sanitizers: `GS_SANITIZE` → GCC/Clang `-fsanitize=address,undefined`; MSVC `/fsanitize=address` (no UBSan).
- Windows link libs: `ws2_32` (hiredis sockets), `shlwapi` not needed. Embed an application manifest (`src/platform/graphstate.manifest`, added to the executable sources on WIN32) with `<longPathAware>true</longPathAware>` and `<activeCodePage>UTF-8</activeCodePage>`.
- `GS_WITH_CPP_GRAMMAR` defaults **OFF on Windows debug** (cl.exe on a 17 MB `parser.c` is very slow); ON in release/CI.
- hiredis on Windows: builds with MSVC via its own CMake (uses its `sockcompat` layer). **Day-1 verification V11.** Fallback if it fails: `src/db/resp.cpp` — a ~300-line RESP2 client over `platform::Socket` (only needs: connect with timeout, write argv as RESP array, read reply tree: `+ - : $ *` types). Keep `GraphClient`'s interface identical so nothing else changes.

## 12.5 Platform layer (`src/platform/`)

Only these files may contain `#ifdef _WIN32` / POSIX headers. Everything else uses this API.

```
src/platform/
├── platform.hpp          # API below
├── platform_posix.cpp    # Linux + macOS (compiled when NOT WIN32)
├── platform_win.cpp      # Windows (compiled when WIN32)
└── graphstate.manifest   # Windows app manifest (long paths, UTF-8 code page)
```

```cpp
namespace gs::platform {
namespace fs = std::filesystem;

// ── stdio & lifecycle ────────────────────────────────────────────────
void init_stdio();                         // Win: _setmode(_fileno(stdin/stdout/stderr), _O_BINARY); SetConsoleOutputCP(CP_UTF8)
                                           // POSIX: no-op. MUST be the first call in main().
void on_shutdown_request(std::function<void()> cb); // POSIX: sigaction(SIGINT,SIGTERM) → sets atomic flag + calls cb from a watcher thread
                                                    // Win: SetConsoleCtrlHandler (CTRL_C/BREAK/CLOSE/SHUTDOWN events)
[[noreturn]] void hard_exit(int code);     // POSIX _exit; Win TerminateProcess(GetCurrentProcess(), code) — crash injection only

// ── paths & strings ──────────────────────────────────────────────────
std::string to_utf8(const fs::path& p);    // via p.u8string(); NEVER use p.string() (ANSI code page on Windows)
fs::path from_utf8(std::string_view s);    // via std::u8string
fs::path executable_path();                // /proc/self/exe | _NSGetExecutablePath+realpath | GetModuleFileNameW
bool fs_case_insensitive(const fs::path& dir); // probe file in .graphstate/: create "gs_CaseProbe", test exists("gs_caseprobe")

// ── files ────────────────────────────────────────────────────────────
struct FileStat { bool exists = false, regular = false; uint64_t size = 0; int64_t mtime_ns = 0; };
FileStat stat(const fs::path&);            // std::filesystem; mtime from last_write_time → ns since Unix epoch (clock_cast)
void write_file_durable(const fs::path& p, std::string_view bytes);   // create/truncate, write all, fsync | FlushFileBuffers, close
void replace_file(const fs::path& src_tmp, const fs::path& dst);      // POSIX rename(2)
        // Win: MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        //      on ERROR_SHARING_VIOLATION / ERROR_ACCESS_DENIED / ERROR_LOCK_VIOLATION: retry 20× with 25 ms sleep,
        //      then throw gs::Error(WRITE_FAILED, "file is locked by another process", {path, winerr})
void remove_file(const fs::path&);         // same retry policy on Windows
void fsync_dir(const fs::path& dir);       // POSIX open(O_RDONLY|O_DIRECTORY)+fsync; Win no-op (NTFS journals metadata)
bool hardlink_or_copy(const fs::path& from, const fs::path& to);  // link() | CreateHardLinkW; fallback fs::copy_file; returns true if linked
struct FileMode { uint32_t posix_mode = 0644; bool readonly = false; };
FileMode file_mode(const fs::path&);       // POSIX st_mode & 07777 | Win: FILE_ATTRIBUTE_READONLY
void set_file_mode(const fs::path&, const FileMode&);

// ── processes ────────────────────────────────────────────────────────
struct ProcResult { int exit_code = -1; std::string out, err; bool timed_out = false; };
ProcResult run_capture(const std::vector<std::string>& argv, const fs::path& cwd, int timeout_ms);
        // POSIX posix_spawnp + pipes + poll; Win CreateProcessW + anonymous pipes, argv quoted per
        // CommandLineToArgvW rules (quote args with spaces/quotes; double backslashes before quotes)

// ── misc ─────────────────────────────────────────────────────────────
uint64_t peak_rss_bytes();                 // Linux getrusage ru_maxrss*1024 | macOS ru_maxrss (bytes) | Win PeakWorkingSetSize
std::string os_name();                     // "windows" | "macos" | "linux" (status/doctor)
}
```

## 12.6 Paths & file-system semantics (MUST)

| Concern | Rule |
|---|---|
| Internal ids | Always workspace-relative, `/`-separated, on-disk casing (`src/Billing.py`). Never store `\` or absolute paths in the graph. |
| Agent-supplied paths | Accept `/` and `\`; normalize `\` → `/`; collapse `./`; reject absolute (`/x`, `C:\x`, `C:/x`), drive-relative (`C:x`), UNC (`\\server\x`, `//server/x`), and any `..` segment → `PATH_OUTSIDE_WORKSPACE`. |
| Case-insensitive FS (Windows NTFS, macOS APFS default) | Detected at startup (`fs_case_insensitive`). FactStore keeps `lowercase(id) → id`; a path differing only in case resolves to the existing on-disk id. Glob matching (ignore/protected/rules `paths`) is case-insensitive on such FS. Creating `Billing.py` when `billing.py` exists → `FILE_EXISTS`. |
| Portable file names | `create_file`/`move_file` targets are rejected (`PATH_INVALID`) on **all** platforms if any segment: is a Windows reserved name (`CON PRN AUX NUL COM1–COM9 LPT1–LPT9`, case-insensitive, with or without extension), ends with `.` or space, or contains `< > : " \| ? *` or control chars. Keeps repos portable across the team's OSes. |
| Containment | `weakly_canonical(root / rel)` must have `root` as prefix (component-wise compare, case-insensitive on case-insensitive FS); for existing files also `canonical()` (resolves symlinks / junctions). |
| Long paths | Windows: manifest `longPathAware` + `LongPathsEnabled=1` (doctor checks the registry value and warns). |
| Unicode | All paths cross the platform boundary as UTF-8 (`to_utf8`/`from_utf8`). Unicode normalization (NFC vs NFD on macOS) is not performed. `// ponytail: no NFC normalization; add if a team mixes macOS-created non-ASCII names with Linux.` |
| Symlinks | Scanner does not follow directory symlinks/junctions. Gate refuses to write through a symlink whose target is outside the workspace (containment check). |

## 12.7 Text & line endings

- Hashes are over raw bytes (CRLF and LF files hash differently — correct; it's what's on disk).
- P1 preserves each existing file's EOL style (§06.4); `replace_text` matching is EOL-insensitive.
- New files (`create_file`): `gate.new_file_eol` = `"auto"` (default) | `"lf"` | `"crlf"`. `auto` = EOL style of the majority of files in the same directory, else of the workspace, else LF. (On Windows with `core.autocrlf=true`, the working tree is CRLF; `auto` matches it.)
- MCP stdio: binary mode on Windows (no `\n`→`\r\n` translation on output). The reader strips one trailing `\r` from each line (some Windows clients send CRLF).
- Tree-sitter grammars handle CRLF natively; line/col computation counts `\r\n` as one line break.
- Source files of this repo: `.gitattributes` with `* text=auto eol=lf`, `*.bat text eol=crlf`, `*.ps1 text eol=crlf` (so Windows checkouts don't break shell scripts or golden fixtures). Golden fixtures are compared after normalizing EOL.

## 12.8 Timestamps & change detection

- `mtime_ns` via `std::filesystem::last_write_time` + `std::chrono::clock_cast<std::chrono::system_clock>` (MSVC, libstdc++ 13+; for older libstdc++ use the `file_clock::to_sys` fallback, guarded by `__cpp_lib_chrono`).
- Resolution differs (NTFS 100 ns, ext4 1 ns, APFS 1 ns, FAT/exFAT 2 s). **Racy-clean rule:** if a file's mtime is within 2 s of the time it was last hashed, the watcher re-hashes it on the next tick even if size+mtime look unchanged.

## 12.9 Docker on every OS

- FalkorDB always runs as a **Linux** container (Windows: Docker Desktop with WSL2 backend; "Windows containers" mode is not supported — doctor detects `docker info` → `OSType: windows` and fails with a hint).
- Named volume (not a bind mount) for data: avoids slow/unsafe fsync semantics of Windows→WSL2 bind mounts.
- Ports bound to `127.0.0.1` only.
- `graphstate doctor` runs `docker version --format '{{.Server.Os}}'` via `run_capture` (skipped if docker CLI absent; DB check still works if FalkorDB is reachable some other way).

## 12.10 Hosts on Windows (paths used by `graphstate init`)

| Host | Windows location |
|---|---|
| Claude Code project config | `<root>\.mcp.json`, `<root>\.claude\settings.json` (same as other OSes) |
| Claude Desktop | `%APPDATA%\Claude\claude_desktop_config.json` |
| Cursor | `<root>\.cursor\mcp.json` / `%USERPROFILE%\.cursor\mcp.json` |
| Antigravity | `<root>\.agents\mcp_config.json` / `%USERPROFILE%\.gemini\config\mcp_config.json` **(verify)** |

- `{{GRAPHSTATE_BIN}}` and `{{ROOT}}` are written with **forward slashes** (`C:/Users/me/tools/graphstate.exe`) — valid for Windows APIs and needs no JSON escaping.
- Hook commands are written as `"C:/…/graphstate.exe" hook deny-edit --host claude-code` (quoted path: user dirs often contain spaces).
- Hook exit code 2 (Claude Code block) works identically on Windows.

## 12.11 Tests & CI across platforms

- No `fork()` anywhere (Windows). Crash-injection tests (§10.2 `test_disk_txn`) re-launch the test binary itself as a child: `run_capture({executable_path(), "--gs-child", point, tmpdir})` with env `GRAPHSTATE_CRASH_AT=point`; the child performs the write and dies at the injection point; the parent then runs recovery and asserts.
- `tests/e2e/run.py` uses `sys.executable`, `subprocess` with binary pipes, and finds `graphstate` / `graphstate.exe`.
- File-locking test (Windows only, `#ifdef _WIN32` in the test): hold the target open without `FILE_SHARE_DELETE` for 200 ms in a thread → `replace_file` retries and succeeds after release; held for 2 s → `WRITE_FAILED` and WAL rollback leaves the original intact.

`.github/workflows/ci.yml`:
```yaml
name: ci
on: [push, pull_request]
jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        include:
          - { os: windows-2022, preset: windows-msvc-release }
          - { os: macos-14,     preset: macos-release }
          - { os: ubuntu-24.04, preset: linux-release }
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: ilammy/msvc-dev-cmd@v1
        if: runner.os == 'Windows'
      - uses: seanmiddleditch/gha-setup-ninja@v5
      - run: cmake --preset ${{ matrix.preset }}
      - run: cmake --build --preset ${{ matrix.preset }}
      - run: ctest --preset ${{ matrix.preset }} -L unit
      - run: python3 tests/e2e/run.py --no-db --bin build/${{ matrix.preset }}/graphstate
        shell: bash
  db-tests:
    runs-on: ubuntu-24.04
    services:
      falkordb:
        image: falkordb/falkordb-server:6.0.2
        ports: ["6390:6379"]
    steps:
      - uses: actions/checkout@v4
      - uses: seanmiddleditch/gha-setup-ninja@v5
      - run: cmake --preset linux-release && cmake --build --preset linux-release
      - run: ctest --preset linux-release -L "db|e2e"
```
(GitHub's Windows/macOS runners can't run Linux service containers, so DB tests run on Linux CI; on the Windows dev machine they run locally against Docker Desktop.) Pin action versions on day 1.

## 12.12 Day-1 platform verification (add to §11.2)

| # | Check |
|---|---|
| V11 | hiredis v1.4.1 builds with MSVC (static) and `PING` works against Docker Desktop FalkorDB from Windows |
| V12 | tree-sitter core + all grammars compile with cl.exe (`/bigobj`); measure tree-sitter-cpp compile time |
| V13 | `MoveFileExW` replace of a file open in VS Code / Notepad++ succeeds (they open with share-delete) |
| V14 | Claude Code on Windows: MCP stdio with binary mode; hook exit-2 block works; path with spaces in `{{GRAPHSTATE_BIN}}` |
| V15 | Antigravity on Windows: config path and hook stdin shape |
