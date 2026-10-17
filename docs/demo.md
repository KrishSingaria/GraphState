# GraphState: 3-Minute Hackathon Demo Script & Walkthrough

> **Companion guide for the 3-minute hackathon demo video submission.**  
> Matches requirements in `docs/design/01-product-and-scope.md` (§01.8) and `docs/design/11-build-plan.md` (§11.7).

---

## Demo Overview

| Timestamp | Scene / Action | Screen Visual | Voiceover Key Points |
|---|---|---|---|
| **0:00 - 0:10** | Title Card | Terminal & GraphState Logo | *"This is GraphState: a deterministic bouncer and semantic graph memory for AI coding agents, powered by FalkorDB."* |
| **0:10 - 0:30** | The Code Graph | FalkorDB Browser (`:3000`) showing `CALLS` graph | *"Instead of raw text files, GraphState models your entire repository as an interconnected semantic call graph in FalkorDB."* |
| **0:30 - 1:00** | Signature Change Rejected | Claude Code: "Add a required currency parameter to calculate_total" | *"When an agent tries to change a function signature, GraphState inspects the blast radius and instantly rejects it with exact caller coordinates."* |
| **1:00 - 1:20** | Multi-File Atomic Apply | Agent submits atomic multi-file patch | *"The agent updates all callers in a single atomic patch. Verified and applied in milliseconds."* |
| **1:20 - 1:40** | Hallucination Prevention | Agent calls non-existent `utils.format_money` | *"When an agent hallucinates a function, GraphState blocks it immediately with `MISSING_SYMBOL` and suggests the right export."* |
| **1:40 - 1:55** | Automated Pitfalls in FalkorDB | FalkorDB Browser showing `Pitfall` node | *"Failures aren't lost. They automatically materialize as `Pitfall` nodes in FalkorDB linked to offending functions."* |
| **1:55 - 2:10** | Crash Resilience Test | `kill -9` engine + restart FalkorDB | *"Disk is always authoritative. Even on sudden crash, the Write-Ahead Journal guarantees zero corrupted files."* |
| **2:10 - 2:25** | Session Briefing & Memory | New session: `get_briefing` called | *"On new session startup, `get_briefing` reminds the agent of past pitfalls, active conventions, and user preferences."* |
| **2:25 - 2:40** | Changeset Mode | Multi-step refactor using Changesets | *"For greenfield APIs, changeset mode manages forward references with obligations until all parts compile cleanly."* |
| **2:40 - 2:50** | Evaluation Results | Milestone 10a Eval Table | *"In my benchmark eval, GraphState eliminated 100% of broken references and cut regression debug tokens by 36%."* |
| **2:50 - 3:00** | Benchmark & Outro | `graphstate bench` terminal output | *"Under 9 MB RAM, sub-millisecond graph traversals. Fully open source under Apache 2.0."* |

---

## Detailed Step-by-Step Walkthrough

### Pre-requisites & Setup
Ensure FalkorDB is running and the workspace is reset to pristine state:
```bash
# 1. Start FalkorDB
docker compose up -d

# 2. Reset benchmark project to pristine state
python tools/demo_reset.py --wipe-memory --reindex

# 3. Open FalkorDB Browser
# Open browser to http://localhost:3000
```

---

### Step 1: Visualizing the Repository as a Graph (0:10)
In FalkorDB Browser (`http://localhost:3000`), execute:
```cypher
MATCH (a:Function)-[r:CALLS]->(b:Function)
RETURN a.id, b.id, r.call_text
```
*Visual: FalkorDB Browser renders the interactive node-link visualization of `benchmark_project` functions and callers.*

---

### Step 2: The Agent Breaks a Signature (0:30)
In Claude Code or agent CLI prompt:
> **Prompt:** *"In `billing.py`, update `calculate_total` to require a new positional parameter `currency`."*

**Agent Action:** The agent tries to edit only `billing.py`.  
**GraphState Result:** **REJECTED**
```json
{
  "status": "REJECTED",
  "reason": "GATE_VIOLATION",
  "violations": [
    {
      "code": "CALLER_ARITY_MISMATCH",
      "message": "Call to 'calculate_total' in 'app.py' missing required parameter 'currency'",
      "file": "app.py",
      "line": 42,
      "col": 12
    },
    {
      "code": "CALLER_ARITY_MISMATCH",
      "message": "Call to 'calculate_total' in 'checkout.py' missing required parameter 'currency'",
      "file": "checkout.py",
      "line": 18,
      "col": 8
    }
  ]
}
```

---

### Step 3: Atomic Multi-File Self-Correction (1:00)
The agent reads the rejection diagnostics and submits a coordinated patch across `billing.py`, `app.py`, and `checkout.py`.

**GraphState Result:** **APPLIED**
- Verified AST across all 3 files.
- Atomic two-phase commit updates working tree.
- Graph synchronized in **~10 ms**.

---

### Step 4: Catching Hallucinated Functions (1:20)
In agent prompt:
> **Prompt:** *"Format currency using `utils.format_money`."*

**GraphState Result:** **REJECTED**
```json
{
  "status": "REJECTED",
  "reason": "GATE_VIOLATION",
  "violations": [
    {
      "code": "MISSING_SYMBOL",
      "message": "Function 'format_money' does not exist in 'utils.py'.",
      "did_you_mean": ["utils.py::format_currency"]
    }
  ]
}
```

---

### Step 5: Automated Memory & Briefing (1:40 - 2:10)
In FalkorDB Browser:
```cypher
MATCH (p:Pitfall)-[:ABOUT]->(s:Function)
RETURN p.code, p.lesson, s.id
```
*Visual: Shows the new `Pitfall` node bound to `calculate_total`.*

Now simulate an engine restart:
```bash
# Restart engine
./build/windows-msvc-release/graphstate.exe doctor
```

In new agent session, agent calls `get_briefing`:
```json
{
  "workspace": "benchmark_project",
  "active_pitfalls": [
    {
      "symbol": "billing.py::calculate_total",
      "lesson": "Signature changes to calculate_total require updating callers in app.py and checkout.py in the same patch."
    }
  ],
  "user_preferences": [
    "Always include currency string in pricing calculations."
  ]
}
```

---

### Step 6: Performance Benchmark (2:50)
Run the live benchmark command in terminal:
```bash
./build/windows-msvc-release/graphstate.exe bench --root tests/fixtures/benchmark_project --runs 20
```

**Output:**
```
================ GraphState Performance Benchmark ================
Workspace: benchmark_project (8 files, 27 symbols)

Workload               N    p50 (ms)    p95 (ms)    p99 (ms)   Notes
---------------------------------------------------------------------------
cold_index             1      125.59      125.59      125.59   63 files/s
rss                    1        8.99        8.99        8.99   8 MB RAM
reindex_one           20        0.10        0.31        0.31   0.1 ms!
read_symbol           20       <0.01        0.02        0.02   Instant
get_impact_d3         20       <0.01        0.01        0.01   Instant
patch_reject          20        0.25        0.41        0.41   Syntax gate
patch_apply           20        3.75        5.93        5.93   Atomic commit
restart                1      118.03      118.03      118.03   Warm reconcile
---------------------------------------------------------------------------
```

---

### Step 7: Resetting for Another Run
To repeat the demo cleanly:
```bash
python tools/demo_reset.py --wipe-memory --reindex
```
*(Runs reliably end-to-end multiple times consecutively).*
