# Agent Instructions for GraphState Repository

## Architecture & Code Guidelines
1. **Language & Standard**: C++20 on MSVC, Clang, GCC.
2. **Platform Boundary**: No OS-specific headers (`windows.h`, `unistd.h`, etc.) outside `src/platform/`. Use `gs::platform::*` API.
3. **Namespaces**: `gs::core`, `gs::db`, `gs::lang`, `gs::index`, `gs::gate`, `gs::memory`, `gs::mcp`, `gs::cli`. Engine is `gs::Engine`.
4. **Error Handling**:
   - Unexpected / environment errors: throw `gs::Error{ErrorCode, message, json_data}`.
   - Gate violations are data (`Violation`), never exceptions.
   - Catch `gs::Error` and `std::exception` at MCP/CLI boundary.
5. **Ownership & Memory**: Strict RAII everywhere; no raw `new`. Wrap tree-sitter types in `std::unique_ptr` with custom deleters (`ts_raii.hpp`).
6. **Strings & Paths**:
   - `std::string` holds UTF-8 bytes.
   - Internal symbol / file IDs are always workspace-relative and `/`-separated with exact on-disk casing.
   - Never store backslashes `\` or absolute paths in the graph.
7. **FalkorDB Queries**:
   - Parameterize all queries using `cypher_params`.
   - Never query FalkorDB in a per-symbol loop; batch with `UNWIND $rows` and pipeline.
   - Plan cache default is 64; avoid constructing dynamic unparameterized Cypher strings.
8. **Milestone Order**:
   - Implement milestones in order (M0 -> M10) per `docs/design/11-build-plan.md`.
   - Run acceptance checks before starting next milestone.
   - Make a git commit after each milestone.
