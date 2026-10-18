# GraphState: 3-Minute Hackathon Demo Script & Walkthrough

> **GraphState is a deterministic C++20 code gatekeeper and persistent graph memory engine for AI coding agents, powered by FalkorDB, ensuring zero broken callers, atomic commits, and robust coding in mission-critical systems.**
>
> **Companion guide for the 3-minute hackathon demo video submission (WeMakeDevs × FalkorDB).**  
> Formatted as a complete **word-for-word voiceover script** with precise screen recording actions, terminal setup guides, and FalkorDB Browser inspection steps.

---

## ⏱️ Video Timeline at a Glance

```text
[0:00 - 1:00]  PART 1: HTML Pitch Deck (Overview, Critical Systems, Tech Stack, 50/50 Results)
[1:00 - 2:30]  PART 2: Live Claude Code Demo on Haiku (Solving OSS-01 with Atomic Gate Protection)
[2:30 - 3:00]  PART 3: FalkorDB Browser Code Graph & Closing Conclusion
```

---

## 🛠️ Pre-Recording Setup & Commands (Keep Ready)

Before you hit record, execute these steps in your terminal:

### 1. Launch FalkorDB and Browser
```powershell
docker compose up -d
```
- FalkorDB runs on `localhost:6390`.
- FalkorDB Browser runs on `http://localhost:3000`. Open this in a browser tab.

### 2. Prepare the Pristine Demo Workspace
```powershell
# In PowerShell:
if (Test-Path demo_requests) { Remove-Item -Recurse -Force demo_requests }
Copy-Item -Recurse eval/oss_workspace/requests demo_requests
cd demo_requests
```

### 3. Initialize GraphState for Claude Code
```powershell
# Automatically injects .mcp.json and CLAUDE.md
G:\GraphState\build\windows-msvc-release\graphstate.exe init --host claude-code --root .
G:\GraphState\build\windows-msvc-release\graphstate.exe index --root .
```

### 4. Open the HTML Pitch Deck
Open [`docs/pitch_deck.html`](pitch_deck.html) in your browser (Chrome/Edge/Brave).  
Press **`F`** to enter fullscreen mode. You are now ready to record!

---

## 🎙️ Word-for-Word Video Script (0:00 - 3:00)

---

### PART 1: The Pitch Deck (0:00 - 1:00)

#### Scene 1: Hero & Identity (0:00 - 0:15)
- **Screen Action:** Browser on Slide 1 of `docs/pitch_deck.html`.
- **Spoken Voiceover:**
  > *"Hi everyone! This is **GraphState**—a deterministic C++20 code gatekeeper and persistent graph memory engine for AI coding agents, powered by FalkorDB.*
  >
  > *AI coding agents are incredible at generating syntax, but they edit code in an architectural vacuum. When an agent changes a core function signature, it routinely breaks callers three layers deep across the codebase. GraphState acts as an uncompromising bouncer: it verifies every patch against a live FalkorDB call graph and blocks uncompilable code before it ever touches disk."*

#### Scene 2: Why Gatekeeping for Critical Systems? (0:15 - 0:30)
- **Screen Action:** Press `ArrowRight` to transition to Slide 2.
- **Spoken Voiceover:**
  > *"We call this the '3 A.M. Bug'. In mission-critical environments—like fintech transaction engines, healthcare infrastructure, or core shared libraries—an agent making trial-and-error edits directly on disk is dangerous. A single unverified signature change breaks downstream services and crashes production.*
  >
  > *While tools like CodeGraph are fantastic for reading code context, read access alone cannot prevent subtle regressions. GraphState introduces deterministic gatekeeping: verifying AST syntax, call bindings, and blast-radius reachability before code is committed."*

#### Scene 3: Why C++20 and FalkorDB? (0:30 - 0:45)
- **Screen Action:** Press `ArrowRight` to transition to Slide 3.
- **Spoken Voiceover:**
  > *"To ensure conversational agents never experience latency lag, the entire gatekeeper is built in C++20 with strict RAII, operating under an ultra-lean 9 megabytes of RAM with sub-millisecond Tree-sitter parsing.*
  >
  > *FalkorDB is our core relational compute engine. It executes multi-hop Cypher traversals in under ten microseconds, runs in-graph PageRank to identify load-bearing symbols, and maintains dual-graph persistent memory so agents never repeat past mistakes."*

#### Scene 4: 50/50 Real-World Results & Problem Setup (0:45 - 1:00)
- **Screen Action:** Press `ArrowRight` to transition to Slide 4, then Slide 5.
- **Spoken Voiceover:**
  > *"In our empirical benchmark across 10 non-trivial refactoring tasks on the requests library, GraphState achieved a 100% success rate with zero broken references, while cutting complex refactoring turns by 52%.*
  >
  > *Now, let's see it live! On Slide 5 is Task OSS-01: we are asking Claude Code on the lightweight Haiku model to add a required connection_timeout parameter to BaseAdapter in requests. Notice the trap: Session.send in sessions.py calls this method. If Haiku updates only adapters.py, the gate will stop it."*

---

### PART 2: Live Claude Code Demo (1:00 - 2:30)

#### Scene 5: Launching Claude Code (1:00 - 1:20)
- **Screen Action:** Switch screen from browser to your terminal inside `demo_requests`.
- **Command Run:**
  ```powershell
  claude --model claude-3-5-haiku
  ```
- **Spoken Voiceover:**
  > *"I'm opening Claude Code running on Haiku right here in our pristine requests repository. GraphState is automatically hooked up via the Model Context Protocol."*

#### Scene 6: Submitting the Task Prompt (1:20 - 1:40)
- **Screen Action:** Paste the `OSS-01` prompt into Claude Code:
  ```text
  In requests/adapters.py, add a required `connection_timeout` parameter to `BaseAdapter.send(self, request, connection_timeout, stream=False, timeout=None, verify=True, cert=None, proxies=None)` (position 2, right after request). Update `HTTPAdapter.send` to match this signature. Ensure callers across the repository support and forward connection_timeout, defaulting to None if omitted.
  ```
- **Spoken Voiceover:**
  > *"We give Haiku the refactoring prompt. Notice what happens immediately: Haiku calls `gs:get_briefing` to load architectural invariants and past pitfalls from FalkorDB.*
  >
  > *Next, it inspects `BaseAdapter.send`. Instead of grepping through hundreds of files, GraphState's `gs:get_impact` instantly reveals that `Session.send` in `sessions.py` is a direct downstream caller."*

#### Scene 7: Atomic Multi-Caller Commit (1:40 - 2:30)
- **Screen Action:** Claude Code runs `gs:propose_patch` with edits covering both `adapters.py` and `sessions.py`. GraphState validates and applies the patch.
- **Spoken Voiceover:**
  > *"Because the agent knows the blast radius upfront, it submits an atomic patch updating both `adapters.py` and `sessions.py` together.*
  >
  > *GraphState's 5-phase gate verifies the AST syntax, validates call arity, ensures zero broken exports, and commits the patch atomically using our Write-Ahead Journal. Zero broken callers, zero uncompilable intermediate state on disk, and all tests pass cleanly!"*

---

### PART 3: FalkorDB Browser & Conclusion (2:30 - 3:00)

#### Scene 8: The Live Graph in FalkorDB Browser (2:30 - 2:45)
- **Screen Action:** Switch to browser tab at `http://localhost:3000` (FalkorDB Browser). Run Cypher query:
  ```cypher
  MATCH (c:Function)-[r:CALLS]->(t:Function)
  RETURN c, r, t LIMIT 25
  ```
- **Spoken Voiceover:**
  > *"Now let's switch to the FalkorDB Browser. Here is our live codebase graph. Every function, class, and caller edge is modeled directly as graph nodes and relationships in FalkorDB.*
  >
  > *Here is `BaseAdapter.send`, and here is the incoming `CALLS` edge from `Session.send`. Any rejected edit automatically materializes as an in-graph `Pitfall` node, permanently warning future agent sessions."*

#### Scene 9: Closing Outro (2:45 - 3:00)
- **Screen Action:** Pan slightly on the FalkorDB graph or show the GitHub repository page ([github.com/KrishSingaria/GraphState](https://github.com/KrishSingaria/GraphState)).
- **Spoken Voiceover:**
  > *"By shifting from passive context reading to deterministic AST gatekeeping with FalkorDB and C++20, GraphState transforms AI coding agents from unconstrained text generators into reliable, architecturally sound engineering partners.*
  >
  > *GraphState is 100% open source under Apache 2.0 with prebuilt binaries for Windows, macOS, and Linux. Thanks for watching!"*

---

## 🎯 Quick Speaker Tips for a Flawless Recording
1. **Fullscreen Pitch Deck:** In Chrome or Edge, open `docs/pitch_deck.html` and hit `F` or `F11` so the browser address bar disappears. Use `ArrowRight` smoothly.
2. **Terminal Font Size:** Increase terminal font size (`Ctrl + +`) in Windows Terminal so viewers on mobile screens can read Claude Code output clearly.
3. **Pacing:** Speak at a natural, confident pace. The script is timed for ~130 words per minute, easily fitting within the 3:00 limit.
4. **FalkorDB Browser Tab:** Keep `http://localhost:3000` open in a separate tab with the query pre-typed so you can click "Run Query" instantly at 2:30!
