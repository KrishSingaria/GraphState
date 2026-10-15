# 03 — Repository Layout, Build, Config, CLI

> Index: [README.md](README.md). All versions verified 2026-10-14 (see §3.3).

## 3.1 Repository tree (every file the build creates)

```
graphstate/
├── CMakeLists.txt                     # top-level build (§3.4)
├── CMakePresets.json                  # windows-msvc-*, linux-release, macos-release, dev-asan (§12.3)
├── .gitattributes                     # eol rules (§12.7)
├── .github/workflows/ci.yml           # Windows + macOS + Linux matrix (§12.11)
├── cmake/
│   ├── Dependencies.cmake             # FetchContent declarations, pinned
│   ├── Grammars.cmake                 # one OBJECT lib per tree-sitter grammar → gs_grammars
│   ├── Embed.cmake                    # gs_embed(): files → generated header of constexpr string_views
│   └── Flags.cmake                    # per-compiler warnings, MSVC static CRT (§12.4)
├── docker-compose.yml                 # FalkorDB with persistence + Browser (§3.6)
├── .clang-format                      # BasedOnStyle: LLVM, ColumnLimit: 110, IndentWidth: 2
├── .gitignore                         # build/, .graphstate/, benchmark_project/.graphstate/, eval/runs/
├── LICENSE                            # Apache-2.0
├── README.md                          # hackathon README (§11.5): setup, data model, Cypher, tracks, AI disclosure
├── AGENTS.md                          # rules for agents working ON this repo (not the product template)
├── docs/
│   ├── design/                        # this spec
│   └── blog.md                        # blog post draft (§11.6)
├── src/
│   ├── main.cpp                       # CLI entry → platform::init_stdio(); cli::run(argc, argv)
│   ├── platform/                      # ONLY place with OS-specific code (§12.5)
│   │   ├── platform.hpp  platform_posix.cpp  platform_win.cpp  graphstate.manifest
│   ├── core/
│   │   ├── config.hpp / config.cpp    # Config struct, load defaults → file → env → CLI
│   │   ├── log.hpp / log.cpp          # Logger (stderr + file), levels, structured key=value
│   │   ├── error.hpp                  # gs::Error {code, message, data}; error code enum
│   │   ├── hash.hpp                   # xxh3_64 hex helpers (XXH_INLINE_ALL)
│   │   ├── paths.hpp / paths.cpp      # workspace-relative paths, containment checks, glob match
│   │   ├── clock.hpp                  # now_ms(), Stopwatch
│   │   ├── ids.hpp / ids.cpp          # patch/session/intent/memory id generators, symbol id helpers
│   │   └── text.hpp / text.cpp        # dedent, reindent, line index, edit distance, ident splitting
│   ├── db/
│   │   ├── graph_client.hpp/.cpp      # hiredis wrapper: connect, query, ro_query, pipeline, multi/exec, reconnect
│   │   ├── cypher_params.hpp/.cpp     # nlohmann::json → "CYPHER k=v …" literal encoder
│   │   ├── result.hpp/.cpp            # parse verbose reply: header/rows/stats; toJSON column → json
│   │   ├── queries.hpp                # ALL Cypher text as constexpr std::string_view (§04.7–04.9)
│   │   └── schema.hpp/.cpp            # ensure indexes/constraints, Meta node, migrations
│   ├── lang/
│   │   ├── language.hpp/.cpp          # enum Language, extension map, LanguageRegistry (TSLanguage*, compiled queries)
│   │   ├── ts_raii.hpp                # unique_ptr deleters for TSParser/TSTree/TSQuery/TSQueryCursor
│   │   ├── parser.hpp/.cpp            # thread_local parser pool, parse(lang, bytes) → Tree
│   │   ├── facts.hpp                  # FileFacts, SymbolFact, CallFact, ImportFact, Param, Signature
│   │   ├── extractor.hpp              # interface Extractor { FileFacts extract(...) }
│   │   ├── python_extractor.cpp       # Python AST walk (§05.5.1)
│   │   ├── ts_extractor.cpp           # TS/TSX AST walk (§05.5.2)
│   │   ├── js_extractor.cpp           # JS AST walk (§05.5.3) — shares helpers with ts_extractor via ts_common.hpp
│   │   ├── ts_common.hpp/.cpp         # shared TS/JS helpers (imports, calls, classes)
│   │   ├── binding.hpp/.cpp           # BindingChecker (§06.7.4)
│   │   ├── metrics.hpp/.cpp           # complexity, nesting, loc (§05.8)
│   │   ├── syntax.hpp/.cpp            # collect ERROR/MISSING nodes (§06.5)
│   │   └── py_builtins.inc            # generated list of Python builtins (tools/gen_py_builtins.py)
│   ├── index/
│   │   ├── scanner.hpp/.cpp           # list workspace files (git ls-files or walk), ignore rules
│   │   ├── fact_store.hpp/.cpp        # in-RAM map file → FileFacts (+ stat info), symbol lookup tables
│   │   ├── resolver.hpp/.cpp          # module + call resolution (§05.7) → ResolvedCall edges
│   │   ├── graph_writer.hpp/.cpp      # FileFacts/edges → batched Cypher (§04.7)
│   │   ├── indexer.hpp/.cpp           # full index, reconcile, reindex(files)
│   │   ├── analytics.hpp/.cpp         # PageRank / betweenness / WCC via FalkorDB algo.* (§05.14)
│   │   └── watcher.hpp/.cpp           # polling thread, ungated write audit
│   ├── gate/
│   │   ├── patch.hpp                  # request/result structs (§06.2) + JSON (de)serialization
│   │   ├── gate.hpp/.cpp              # orchestrator P0..P6
│   │   ├── apply.cpp                  # P1 ops
│   │   ├── checks.cpp                 # P2–P4
│   │   ├── rules.hpp/.cpp             # P5 RulesEngine, rules.json loader, predicate eval
│   │   ├── changesets.hpp/.cpp        # changesets, obligations, snapshots (§06.14)
│   │   ├── audit.hpp/.cpp             # audit_workspace(base, current): shared by changesets + `graphstate audit` (§13.4)
│   │   └── disk_txn.hpp/.cpp         # P6 WAL + recovery
│   ├── memory/
│   │   ├── sessions.hpp/.cpp          # Session lifecycle, heartbeat, interrupted marking
│   │   ├── memory_store.hpp/.cpp      # remember/recall/pitfalls/decisions writes + reads
│   │   └── briefing.hpp/.cpp          # get_briefing assembly + markdown rendering
│   ├── mcp/
│   │   ├── transport.hpp/.cpp         # stdio line reader/writer
│   │   ├── server.hpp/.cpp            # JSON-RPC dispatcher, legacy+modern eras (§08.2)
│   │   ├── tool_registry.hpp/.cpp     # Tool {name,title,description,input_schema,annotations,handler}
│   │   ├── schema_validate.hpp/.cpp   # JSON-Schema subset validator for tool args (§08.3)
│   │   ├── tools_read.cpp             # search_symbols, get_file_outline, read_symbol, get_impact, get_module_map
│   │   ├── tools_write.cpp            # propose_patch
│   │   ├── tools_memory.cpp           # get_briefing, remember, recall, recall_pitfalls, end_session
│   │   ├── tools_admin.cpp            # reindex, status
│   │   ├── resources.cpp              # graphstate://briefing, graphstate://rules
│   │   ├── prompts.cpp                # graphstate_workflow
│   │   └── instructions.inc           # server instructions text (§08.4) as raw string literal
│   ├── engine.hpp / engine.cpp        # Engine: owns Config, Logger, Registry, FactStore, Resolver, clients, Gate, Memory, Watcher; state_mu
│   └── cli/
│       ├── cli.hpp / cli.cpp          # argument parsing + subcommand dispatch
│       ├── cmd_serve.cpp  cmd_index.cpp  cmd_status.cpp  cmd_doctor.cpp
│       ├── cmd_query.cpp  cmd_bench.cpp  cmd_forget.cpp  cmd_init.cpp  cmd_hook.cpp  cmd_debug_facts.cpp  cmd_changeset.cpp  cmd_audit.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/                          # doctest, no DB
│   │   ├── test_cypher_params.cpp  test_text.cpp  test_paths.cpp  test_binding.cpp
│   │   ├── test_python_extractor.cpp  test_ts_extractor.cpp  test_js_extractor.cpp
│   │   ├── test_resolver.cpp  test_syntax.cpp  test_apply.cpp  test_rules.cpp
│   │   ├── test_metrics.cpp  test_disk_txn.cpp  test_result_parse.cpp
│   ├── integration/                   # doctest, needs FalkorDB (label "db")
│   │   ├── test_graph_client.cpp  test_graph_writer.cpp  test_gate_e2e.cpp  test_memory.cpp  test_restart.cpp
│   ├── fixtures/                      # small source trees + expected facts JSON (§10.3), benchmark_project_pristine/
│   └── e2e/
│       ├── run.py                     # spawns `graphstate serve`, plays *.jsonl, checks expectations
│       └── *.jsonl                    # scripted MCP conversations (§10.4)
├── tools/
│   ├── gen_py_builtins.py             # writes src/lang/py_builtins.inc (dev-time only)
│   ├── gen_synthetic.py               # synthetic large workspace for bench (§10.6)
│   └── demo_reset.sh                  # resets benchmark_project + graph for the demo
├── host-kits/                         # templates written by `graphstate init` (§09)
│   ├── claude-code/.mcp.json  .claude/settings.json
│   ├── cursor/.cursor/mcp.json  .cursor/hooks.json  .cursor/rules/graphstate.mdc
│   ├── antigravity/.agents/mcp_config.json  .agents/hooks.json  .agents/rules/graphstate.md
│   ├── claude-desktop/claude_desktop_config.json
│   └── AGENTS.md                      # agent rules template for user workspaces
├── benchmark_project/                 # demo workspace (§10.5) — small, for the live demo
└── eval/                              # with-vs-without evaluation harness (§13.8)
```

Templates in `host-kits/` are also compiled into the binary (CMake `file(READ)` → generated header `host_kits.inc`) so `graphstate init` works from an installed binary.

## 3.2 Language & coding conventions (MUST)

- C++20 on **MSVC, Clang and GCC**; warnings-as-errors on our sources only via `gs_apply_warnings()` (§12.4). No OS headers outside `src/platform/` (§12.5).
- Namespaces: `gs::core`, `gs::db`, `gs::lang`, `gs::index`, `gs::gate`, `gs::memory`, `gs::mcp`, `gs::cli`. `Engine` is `gs::Engine`.
- Errors: throw `gs::Error{ErrorCode, std::string message, nlohmann::json data}` for unexpected/environment failures. Gate rule outcomes are **data** (`Violation`), never exceptions. MCP dispatcher catches `gs::Error` and `std::exception` at the boundary.
- Ownership: RAII everywhere; no raw `new`. Tree-sitter handles wrapped in `std::unique_ptr` with custom deleters (`ts_raii.hpp`).
- Strings: `std::string` holds UTF-8 bytes; byte offsets everywhere (tree-sitter native). Columns reported as byte column + 1.
- No global mutable state except `thread_local` parser pool and the logger.
- Logging: `GS_LOG_INFO("indexed", {{"files", n}, {"ms", t}})` → `2026-10-15T10:00:00.123Z INFO indexed files=812 ms=640`.
- One class per header; headers contain declarations shown in this spec; keep functions < 80 lines.

## 3.3 Pinned dependencies

| Dependency | Version (tag) | Use | Build notes |
|---|---|---|---|
| tree-sitter core | `v0.27.1` | parsing | Root `CMakeLists.txt`, target `tree-sitter`; set `BUILD_SHARED_LIBS OFF`, `AMALGAMATED ON` before `FetchContent_MakeAvailable`. API: `ts_parser_set_language` returns `bool`; `ts_language_abi_version`; **no** `*_set_timeout_micros` (removed 0.26) — use `TSParseOptions.progress_callback` if a timeout is ever needed. LANGUAGE_VERSION 15, MIN_COMPATIBLE 13. |
| tree-sitter-python | `v0.25.0` | semantic | `src/parser.c` + `src/scanner.c` |
| tree-sitter-typescript | `v0.23.2` | semantic | `typescript/src/{parser,scanner}.c` and `tsx/src/{parser,scanner}.c`; scanners `#include "../../common/scanner.h"` → keep repo layout intact |
| tree-sitter-javascript | `v0.25.0` | semantic | `src/parser.c` + `src/scanner.c` |
| tree-sitter-go | `v0.25.0` | syntax | `src/parser.c` only |
| tree-sitter-rust | `v0.24.2` | syntax | parser + scanner |
| tree-sitter-java | `v0.23.5` | syntax | parser only |
| tree-sitter-c | `v0.24.2` | syntax | parser only |
| tree-sitter-cpp | `v0.23.4` | syntax | parser + scanner (parser.c ≈ 17 MB — slow compile; see `GS_WITH_CPP_GRAMMAR`) |
| tree-sitter-json | latest tag at build time, pin it in `Dependencies.cmake` | syntax | parser only *(version not verified — pin on day 1)* |
| hiredis | `v1.4.1` | RESP client | target `hiredis::hiredis`; set `BUILD_SHARED_LIBS OFF`, `DISABLE_TESTS ON`, `ENABLE_SSL OFF` |
| nlohmann/json | `v3.12.0` | JSON | target `nlohmann_json::nlohmann_json`; `JSON_BuildTests OFF` |
| xxHash | `v0.8.4` | hashing | header-only: FetchContent download only, `#define XXH_INLINE_ALL` + `#include "xxhash.h"` in `core/hash.hpp` |
| doctest | `v2.5.3` | tests | target `doctest::doctest` |
| FalkorDB | `falkordb/falkordb:6.0.2` (Docker) | graph DB | v6 = Rust engine; GRAPH.* commands & Cypher unchanged from 4.x |

**Do NOT use the grammars' own CMakeLists** (they contain `add_custom_command` that may try to run the `tree-sitter` CLI to regenerate `parser.c`). FetchContent with `SOURCE_SUBDIR` pointing to a non-existent dir (e.g. `SOURCE_SUBDIR _none_`) downloads without adding; then `Grammars.cmake` builds each one.

## 3.4 CMake

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.24)
project(graphstate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(GS_WITH_CPP_GRAMMAR "Build tree-sitter-cpp (slow compile)" ON)
option(GS_SANITIZE "ASan+UBSan for our code" OFF)
option(GS_BUILD_TESTS "Build tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/Grammars.cmake)

file(GLOB_RECURSE GS_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM GS_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)
if(WIN32)
  list(FILTER GS_SOURCES EXCLUDE REGEX "platform_posix\\.cpp$")
else()
  list(FILTER GS_SOURCES EXCLUDE REGEX "platform_win\\.cpp$")
endif()
add_library(graphstate_core STATIC ${GS_SOURCES})
target_include_directories(graphstate_core PUBLIC src ${CMAKE_BINARY_DIR}/generated ${xxhash_SOURCE_DIR})
target_link_libraries(graphstate_core PUBLIC tree-sitter gs_grammars hiredis::hiredis nlohmann_json::nlohmann_json Threads::Threads)
include(cmake/Flags.cmake)
gs_apply_warnings(graphstate_core)
if(WIN32)
  target_link_libraries(graphstate_core PUBLIC ws2_32)
endif()
target_compile_definitions(graphstate_core PUBLIC GS_VERSION="${PROJECT_VERSION}"
                           $<$<BOOL:${GS_WITH_CPP_GRAMMAR}>:GS_WITH_CPP_GRAMMAR=1>)
if(GS_SANITIZE)
  if(MSVC)
    target_compile_options(graphstate_core PUBLIC /fsanitize=address)
  else()
    target_compile_options(graphstate_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(graphstate_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

# embed host-kit templates + server instructions
include(cmake/Embed.cmake)   # defines gs_embed(<out.inc> <files...>) → constexpr string_views
gs_embed(${CMAKE_BINARY_DIR}/generated/host_kits.inc ${CMAKE_SOURCE_DIR}/host-kits)

add_executable(graphstate src/main.cpp $<$<BOOL:${WIN32}>:${CMAKE_SOURCE_DIR}/src/platform/graphstate.manifest>)
target_link_libraries(graphstate PRIVATE graphstate_core)
install(TARGETS graphstate)

if(GS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

### `cmake/Dependencies.cmake`

```cmake
include(FetchContent)
find_package(Threads REQUIRED)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# tree-sitter core
set(AMALGAMATED ON CACHE BOOL "" FORCE)
FetchContent_Declare(treesitter GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
                     GIT_TAG v0.27.1 GIT_SHALLOW TRUE)
# hiredis
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(hiredis GIT_REPOSITORY https://github.com/redis/hiredis.git GIT_TAG v1.4.1 GIT_SHALLOW TRUE)
# json
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
# xxhash (download only)
FetchContent_Declare(xxhash GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git GIT_TAG v0.8.4
                     GIT_SHALLOW TRUE SOURCE_SUBDIR _none_)
FetchContent_MakeAvailable(treesitter hiredis json xxhash)

if(GS_BUILD_TESTS)
  FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.5.3 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(doctest)
endif()
```

### `cmake/Grammars.cmake`

```cmake
# name  repo  tag  [subdir]  has_scanner
set(GS_GRAMMARS
  "python|tree-sitter-python|v0.25.0||1"
  "typescript|tree-sitter-typescript|v0.23.2|typescript|1"
  "tsx|tree-sitter-typescript|v0.23.2|tsx|1"
  "javascript|tree-sitter-javascript|v0.25.0||1"
  "go|tree-sitter-go|v0.25.0||0"
  "rust|tree-sitter-rust|v0.24.2||1"
  "java|tree-sitter-java|v0.23.5||0"
  "c|tree-sitter-c|v0.24.2||0"
  "json|tree-sitter-json|<PIN>||0")
if(GS_WITH_CPP_GRAMMAR)
  list(APPEND GS_GRAMMARS "cpp|tree-sitter-cpp|v0.23.4||1")
endif()

add_library(gs_grammars STATIC)
foreach(entry IN LISTS GS_GRAMMARS)
  string(REPLACE "|" ";" f "${entry}")
  list(GET f 0 name) ; list(GET f 1 repo) ; list(GET f 2 tag) ; list(GET f 3 sub) ; list(GET f 4 scanner)
  string(REPLACE "-" "_" fc_name "${repo}")
  if(NOT DEFINED ${fc_name}_POPULATED_DONE)
    FetchContent_Declare(${fc_name} GIT_REPOSITORY https://github.com/tree-sitter/${repo}.git
                         GIT_TAG ${tag} GIT_SHALLOW TRUE SOURCE_SUBDIR _none_)
    FetchContent_MakeAvailable(${fc_name})
    set(${fc_name}_POPULATED_DONE 1)
  endif()
  set(root "${${fc_name}_SOURCE_DIR}")
  if(sub)
    set(root "${root}/${sub}")
  endif()
  add_library(ts_${name} OBJECT ${root}/src/parser.c $<$<BOOL:${scanner}>:${root}/src/scanner.c>)
  target_include_directories(ts_${name} PRIVATE ${root}/src)
  set_target_properties(ts_${name} PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(ts_${name} PRIVATE /W0 /O2 /bigobj /utf-8)
  else()
    target_compile_options(ts_${name} PRIVATE -O2 -w)     # vendored: no warnings, always optimized
  endif()
  target_sources(gs_grammars PRIVATE $<TARGET_OBJECTS:ts_${name}>)
endforeach()
target_link_libraries(gs_grammars PUBLIC tree-sitter)
```
(`$<BOOL:…>` inside `add_library` source lists works with generator expressions; if a CMake version rejects it, use an `if(scanner)` branch.)

### Build commands

```bash
docker compose up -d                     # FalkorDB + Browser
cmake --preset <windows-msvc-release|linux-release|macos-release>
cmake --build --preset <same>
ctest --preset <same> -L unit            # no DB needed
ctest --preset <same> -L db              # needs FalkorDB on 127.0.0.1:6390
build/<preset>/graphstate doctor         # graphstate.exe on Windows
```

Platforms: **Windows 10/11 (MSVC, primary dev)**, macOS 13+, Linux — full matrix and Windows setup in [12-platform.md](12-platform.md).

## 3.5 Configuration

Precedence (later wins): built-in defaults → `~/.config/graphstate/config.json` → `<root>/.graphstate/config.json` → environment → CLI flags.

### `<root>/.graphstate/config.json` — full schema with defaults

```json
{
  "falkordb": { "host": "127.0.0.1", "port": 6390, "password": "", "connect_timeout_ms": 1000,
                "query_timeout_ms": 10000, "graph_prefix": "gs" },
  "index": {
    "max_file_bytes": 1048576,
    "ignore": ["**/node_modules/**", "**/.git/**", "**/.venv/**", "**/venv/**", "**/__pycache__/**",
               "**/dist/**", "**/build/**", "**/target/**", "**/.graphstate/**", "**/*.min.js"],
    "use_git_ls_files": true,
    "parse_threads": 0
  },
  "python": { "source_roots": ["", "src"] },
  "watch": { "enabled": true, "interval_ms": 500, "rescan_interval_ms": 10000, "bulk_threshold": 20 },
  "gate": {
    "mode": "enforce",
    "protected": [".git/**", ".graphstate/**", "**/.env", "**/.env.*", "**/*.lock", "**/package-lock.json",
                  "**/node_modules/**"],
    "unknown_files": "allow",
    "allow_write_without_db": false,
    "max_edits": 200, "max_files": 100, "max_code_bytes": 1048576, "max_patch_bytes": 8388608,
    "new_file_eol": "auto"
  },
  "memory": { "session_timeout_s": 120, "briefing_max_items": 15, "user_graph": "gs:__user__" },
  "log": { "level": "info", "file": ".graphstate/logs/engine.log", "max_bytes": 10485760, "keep": 3 }
}
```
`parse_threads: 0` → `min(8, hardware_concurrency())`.

### Environment variables

| Var | Overrides |
|---|---|
| `GRAPHSTATE_ROOT` | workspace root (default: cwd) |
| `GRAPHSTATE_FALKOR_HOST` / `_PORT` / `_PASSWORD` | `falkordb.*` |
| `GRAPHSTATE_LOG_LEVEL` | `log.level` (`trace|debug|info|warn|error`) |
| `GRAPHSTATE_GATE_MODE` | `gate.mode` |
| `GRAPHSTATE_NO_WATCH=1` | `watch.enabled=false` |

### Workspace key & graph names

- `workspace_key = sanitize(basename(root)) + "-" + xxh3_hex(realpath(root)).substr(0, 8)`; sanitize = lowercase, `[^a-z0-9_-]` → `_`, max 32 chars. Example: `benchmark_project-3f9a1c2e`.
- Project graph: `<graph_prefix>:<workspace_key>` → `gs:benchmark_project-3f9a1c2e`.
- User graph: `memory.user_graph` → `gs:__user__`.

### `.graphstate/` directory

```
.graphstate/
├── config.json       # optional, user-edited
├── rules.json        # optional, user-edited (§06.8.2)
├── wal/              # write-ahead journal (§06.9) — never edit by hand
├── changesets/       # changeset snapshots (§06.14.5) — never edit by hand
└── logs/engine.log   # rotated
```
`graphstate init` adds `.graphstate/wal/`, `.graphstate/changesets/` and `.graphstate/logs/` to the workspace `.gitignore` (creates it if absent).

## 3.6 `docker-compose.yml`

```yaml
services:
  falkordb:
    image: falkordb/falkordb:6.0.2          # bundles FalkorDB Browser on :3000
    container_name: graphstate-falkordb
    restart: unless-stopped
    ports:
      - "127.0.0.1:6390:6379"               # localhost only: image runs with --protected-mode no
      - "127.0.0.1:3000:3000"               # FalkorDB Browser (demo)
    environment:
      REDIS_ARGS: "--appendonly yes --appendfsync everysec --save 300 10"
      FALKORDB_ARGS: "CACHE_SIZE 64 TIMEOUT_DEFAULT 10000 TIMEOUT_MAX 60000"
    volumes:
      - graphstate_data:/var/lib/falkordb/data   # real --dir (FALKORDB_DATA_PATH); /data is a symlink to it
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 2s
      retries: 20
volumes:
  graphstate_data:
```

Notes:
- `CACHE_SIZE 64`: GraphState has ~45 distinct parameterized queries; default plan cache is 25 (LRU) → would thrash.
- Production/no-UI alternative: `falkordb/falkordb-server:6.0.2` (port 6379 only).
- Durability: AOF `everysec` → at most ~1 s of memory writes lost on a host crash; code facts are re-derived from disk. Users can set `--appendfsync always`.
- *Verify on day 1:* `redis-cli` exists in the image (else healthcheck `CMD-SHELL "echo PING | nc localhost 6379 | grep PONG"`); `--save 300 10` passes through `REDIS_ARGS` word-splitting.

## 3.7 CLI

`graphstate <command> [options]`. Global options: `--root <dir>`, `--config <file>`, `--log-level <lvl>`, `--falkor <host:port>`.

| Command | Purpose | Output | Exit codes |
|---|---|---|---|
| `serve` | MCP server over stdio (what hosts launch). `--no-watch`. | JSON-RPC on stdout only | 0 on EOF |
| `index [--full] [paths…]` | Index/reconcile workspace (or given files). `--full` = delete code subgraph (NOT memory) and rebuild. | `indexed 812 files (34 changed) in 640 ms` | 0 ok, 2 DB unavailable |
| `status [--json]` | Graph name, counts by label, last index, WAL entries, rules errors, session counts, DB latency (PING). | table / JSON | 0, 2 |
| `doctor` | Checks: docker reachable? FalkorDB PING, version (`INFO server`/`MODULE LIST`), AOF on (`CONFIG GET appendonly`), plan cache size, grammars loaded + ABI, workspace writable, host-kit files present. | ✔/✖ checklist + fix hints | 0 all ok, 1 any ✖ |
| `query "<cypher>" [--ro]` | Debug: run Cypher against the project graph; prints rows. | table | 0, 1 query error |
| `bench [--iterations 200]` | Latency benchmark (§10.6). | table p50/p95/p99 | 0 |
| `forget <memory-id> \| --pitfall <key> \| --all-memory --yes` | Delete memories/pitfalls (human-only; there is no MCP tool for this). | summary | 0 |
| `init --host claude-code\|cursor\|antigravity\|claude-desktop [--force]` | Write host-kit files into the workspace (merge JSON if files exist; never overwrite without `--force`). Prints next steps. | file list | 0, 1 conflict |
| `hook deny-edit [--host claude-code\|cursor\|antigravity]` | PreToolUse hook entry point: reads hook JSON from stdin, prints the host-specific deny response (§09). | host-specific | Claude Code: 2 (block); others: 0 + JSON |
| `changeset list\|show <id>\|close --force <id>\|abandon [--force] <id>` | Human control over changesets (§06.14.2). | text | 0, 1 conflict |
| `audit --root <dir> [--base <dir>] [--json]` | Whole-workspace consistency check, no DB needed; with `--base` reports only introduced problems (§13.4). | text / JSON | 0 |
| `version` | `graphstate 0.1.0 (tree-sitter 0.27.1, hiredis 1.4.1, windows-x64)` | text | 0 |
