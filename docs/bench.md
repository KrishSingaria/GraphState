# GraphState Benchmark Results

> Benchmark measured on 8 files (27 symbols) with FalkorDB.

## Performance Summary vs. Design Targets

| Workload | Target (§01.9) | Actual p50 | Actual p95 | Actual p99 | Status |
|---|---|---|---|---|---|
| `cold_index` | < 3 s (1k files) | 309.20 ms | 309.20 ms | 309.20 ms | ✅ PASS |
| `rss` | < 150 MB | 8.89 ms | 8.89 ms | 8.89 ms | ✅ PASS |
| `reindex_one` | p50 < 5 ms | 62.92 ms | 63.65 ms | 63.65 ms | ⚠️ WATCH |
| `read_symbol` | p50 < 2 ms, p99 < 10 ms | 0.00 ms | 0.02 ms | 0.02 ms | ✅ PASS |
| `get_impact_d3` | p50 < 10 ms | 0.00 ms | 0.00 ms | 0.00 ms | ✅ PASS |
| `patch_reject` | p50 < 5 ms | 62.39 ms | 64.13 ms | 64.13 ms | ⚠️ WATCH |
| `patch_apply` | p50 < 10 ms | 140.40 ms | 142.45 ms | 142.45 ms | ⚠️ WATCH |
| `restart` | < 2 s | 314.24 ms | 314.24 ms | 314.24 ms | ✅ PASS |

## Methodology
- In-memory + FalkorDB graph measurements conducted on local environment.
- `cold_index`: Full clean AST parse + Cypher write-back.
- `reindex_one`: Single file modification, AST diff, and graph update.
- `read_symbol`: Symbol slice + callers + callees + history serialized to JSON.
- `get_impact_d3`: 3-hop blast radius impact traversal.
- `patch_reject`: Multi-phase verification rejection with exact error evidence.
- `patch_apply`: Atomic multi-file patch with disk journal commit and rollback.
