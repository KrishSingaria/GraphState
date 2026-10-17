// instructions.inc — Server instructions text (§08.4)
#pragma once
#include <string_view>

namespace gs::mcp {

inline constexpr std::string_view SERVER_INSTRUCTIONS =
R"_(GraphState is the code gateway for this repository. It indexes the code into a FalkorDB graph, verifies every change before it reaches disk, and remembers past sessions.

RULES
1. Start every session with get_briefing (pass your goal). It returns user preferences, project conventions, unfinished work and known pitfalls. Follow them.
2. Write code ONLY through propose_patch. Direct file edits are disabled in this repo; edits made outside the gate are detected and reported. APPLIED means written to the working tree (GraphState never runs git).
3. Before changing a function, call read_symbol on it: you get its exact source, callers, callees, history and pitfalls. Use get_file_outline instead of reading whole files.
4. Before changing a signature or deleting a symbol, call get_impact. Put the definition change AND every caller update in ONE propose_patch (edits may span files; they are written atomically).
5. If propose_patch returns REJECTED, read every violation: each has file:line, evidence and a fix_hint. Fix all of them and resubmit. Do not retry the same patch unchanged.
6. Only call helpers that exist. If unsure, search_symbols or get_file_outline first. Calling a non-existent workspace function is rejected (MISSING_SYMBOL).
7. Use dry_run: true to validate a risky patch without writing.
8. Building something new over several patches (files that reference code you have not written yet), or a refactor spread over many steps? Call changeset {action:"begin", intent} first. Cross-file problems then become obligations instead of rejections; clear them all, then changeset {action:"finalize"}. If an open changeset is listed in the briefing, continue it. changeset {action:"abandon"} restores every file it touched.
9. When you learn a durable user preference or project convention, save it with remember. Do not store secrets.
10. Before finishing, call end_session with a 1-3 sentence summary of what was done and what is left.

PATCH TIPS
- replace_symbol: send the full new definition (decorators/export included), as returned by read_symbol. Indentation is fixed automatically.
- insert_symbol: anchor with after/before/into_class, or omit for end of file.
- replace_text: for imports, constants and non-code files; old_text must match exactly once.
- create_file / delete_file / move_file for whole files. One patch may create many files that reference each other.)_";

} // namespace gs::mcp
