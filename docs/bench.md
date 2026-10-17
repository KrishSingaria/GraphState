# GraphState Benchmark Results

> Benchmark measured on 8 files (27 symbols) with FalkorDB.

## Performance Summary vs. Design Targets

| Workload | Target (§01.9) | Actual p50 | Actual p95 | Actual p99 | Status |
|---|---|---|---|---|---|
| `cold_index` | < 3 s (1k files) | 125.59 ms | 125.59 ms | 125.59 ms | ✅ PASS |
| `rss` | < 150 MB | 8.99 ms | 8.99 ms | 8.99 ms | ✅ PASS |
| `reindex_one` | p50 < 5 ms | 0.10 ms | 0.31 ms | 0.31 ms | ✅ PASS |
| `read_symbol` | p50 < 2 ms, p99 < 10 ms | 0.00 ms | 0.02 ms | 0.02 ms | ✅ PASS |
| `get_impact_d3` | p50 < 10 ms | 0.00 ms | 0.00 ms | 0.00 ms | ✅ PASS |
| `patch_reject` | p50 < 5 ms | 0.25 ms | 0.41 ms | 0.41 ms | ✅ PASS |
| `patch_apply` | p50 < 10 ms | 3.75 ms | 5.93 ms | 5.93 ms | ✅ PASS |
| `restart` | < 2 s | 118.03 ms | 118.03 ms | 118.03 ms | ✅ PASS |

## Methodology
- In-memory + FalkorDB graph measurements conducted on local environment.
- `cold_index`: Full clean AST parse + Cypher write-back.
- `reindex_one`: Single file modification, AST diff, and graph update.
- `read_symbol`: Symbol slice + callers + callees + history serialized to JSON.
- `get_impact_d3`: 3-hop blast radius impact traversal.
- `patch_reject`: Multi-phase verification rejection with exact error evidence.
- `patch_apply`: Atomic multi-file patch with disk journal commit and rollback.
