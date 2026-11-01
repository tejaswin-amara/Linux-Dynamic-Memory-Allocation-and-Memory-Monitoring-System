# TEST_READY: 4-Tier E2E Test Suite Specification & Readiness Report

## Status
- **Test Architecture**: 4-Tier Opaque-Box Comprehensive E2E Test Suite
- **Directory**: `tests/e2e/`
- **Master Shell Runner**: `tests/e2e_runner.sh`
- **Master Python Suite**: `tests/test_e2e_suite.py`
- **Total Assertions**: 141 test cases (Target was > 140)
- **Readiness State**: Fully implemented, verified, deterministic pass/fail signals established.

---

## How to Run the E2E Test Suite
```bash
# Execute complete 4-tier suite (141 tests)
./tests/e2e_runner.sh

# Or directly with Python:
python3 tests/test_e2e_suite.py

# Run specific tiers:
./tests/e2e_runner.sh --tier 1                   # Tier 1 only (60 tests)
./tests/e2e_runner.sh --tier 1 --tier 3 --tier 4  # Tiers 1, 3, and 4 (81 tests)
./tests/e2e_runner.sh --tier 2                   # Tier 2 boundary tests (60 tests)

# Verbose output with detailed test names:
./tests/e2e_runner.sh -v
```

---

## Test Inventory & Tier Coverage Matrix

| # | Feature Area | Requirement Source | Tier 1 (Feature) | Tier 2 (Boundary) | Tier 3 (Pairwise) | Tier 4 (Workload) | Total Tests |
|---|---|---|:---:|:---:|:---:|:---:|:---:|
| 1 | Standard Allocation (`malloc`, `free`) | POSIX.1-2001 / R1 | 5 | 5 | ✓ | ✓ | 10+ |
| 2 | Initialized Allocation (`calloc`) | POSIX.1-2001 / R1 | 5 | 5 | ✓ | — | 10+ |
| 3 | Reallocation (`realloc`, in-place & grow) | POSIX.1-2001 / R1 | 5 | 5 | ✓ | ✓ | 10+ |
| 4 | Aligned Allocations (`posix_memalign`, `aligned_alloc`) | POSIX.1-2001 / C11 | 5 | 5 | ✓ | — | 10+ |
| 5 | Large Block Path (`mmap` threshold $\ge 128$ KB) | Spec / Architecture | 5 | 5 | ✓ | ✓ | 10+ |
| 6 | Coalescing & Fragmentation (forward/backward) | Memory Engine Spec | 5 | 5 | ✓ | ✓ | 10+ |
| 7 | Canary Corruption & Double-Free Protection | Defensive Spec | 5 | 5 | ✓ | — | 10+ |
| 8 | Interposition & Bootstrap Arena (`LD_PRELOAD`) | System Integration | 5 | 5 | ✓ | ✓ | 10+ |
| 9 | Proc Telemetry Parser (CPU, Memory, State) | Linux VFS Spec | 5 | 5 | ✓ | ✓ | 10+ |
| 10 | CLI Telemetry Interface (`--json`, `--headless`) | CLI Spec | 5 | 5 | ✓ | — | 10+ |
| 11 | HTTP Telemetry Daemon (`/api/metrics`) | REST Spec | 5 | 5 | ✓ | ✓ | 10+ |
| 12 | Authenticated Signal Dispatch (`/api/process/signal`) | Control Plane Spec | 5 | 5 | ✓ | ✓ | 10+ |
| **All** | **Cross-Feature & Real-World Scenarios** | Full System Integration | — | — | **16** | **5** | **21** |
| **Total** | **4-Tier Comprehensive E2E Suite** | | **60** | **60** | **16** | **5** | **141** |

---

## Detailed Test Breakdown

### Tier 1: Feature Coverage (60 Tests, 100% Pass)
- `tests/e2e/tier1_feature/test_malloc_free.py`: 5 tests (small allocation, write/read payload integrity, 16B alignment, clean free, block reuse).
- `tests/e2e/tier1_feature/test_calloc.py`: 5 tests (zero-initialization, 16B alignment, write/read verification, free release, non-overlapping blocks).
- `tests/e2e/tier1_feature/test_realloc.py`: 5 tests (growth data preservation, shrink prefix preservation, NULL malloc equivalence, 16B alignment, chained grow/shrink).
- `tests/e2e/tier1_feature/test_aligned.py`: 5 tests (posix_memalign 16B, aligned_alloc 16B, write/read integrity, posix free, aligned free).
- `tests/e2e/tier1_feature/test_mmap.py`: 5 tests (mmap threshold routing, 256KB payload integrity, munmap clean release, coexisting mmap allocations, 2MB allocation).
- `tests/e2e/tier1_feature/test_coalesce.py`: 5 tests (backward coalescing, forward coalescing, bidirectional coalescing, larger block reuse, integrity validation).
- `tests/e2e/tier1_feature/test_canary_doublefree.py`: 5 tests (header canary 0xDEADBEEF, footer canary 0xBEEFDEAD, double-free abort, header canary corruption abort, footer canary corruption abort).
- `tests/e2e/tier1_feature/test_preload.py`: 5 tests (/bin/echo under preload, malloc preload, calloc preload, realloc preload, reallocarray preload).
- `tests/e2e/tier1_feature/test_proc_telemetry.py`: 5 tests (CPU utilization in range [0, 100], core count >= 1, mem_total/mem_available positive, process list PIDs >= 1, process state valid ascii).
- `tests/e2e/tier1_feature/test_cli_telemetry.py`: 5 tests (--help code 0, -h code 0, headless json output, valid json structure, positive process count).
- `tests/e2e/tier1_feature/test_http_metrics.py`: 5 tests (GET /api/metrics 200 OK, expected json keys, cpu and memory metrics, process table array, index.html dashboard serving).
- `tests/e2e/tier1_feature/test_authenticated_signals.py`: 5 tests (valid token signal 0 to self 200 OK, missing token 401, invalid token 401, PID 1 protected, PID 0 protected).

### Tier 2: Boundary & Corner Cases (60 Tests, 58 Pass, 2 Escalated Bugs)
- `tests/e2e/tier2_boundary/test_malloc_free_boundary.py`: 5 tests (malloc(0), free(NULL), malloc(1), power-of-two boundaries 2^k±1, SIZE_MAX overflow rejection).
- `tests/e2e/tier2_boundary/test_calloc_boundary.py`: 5 tests (calloc(0, n), calloc(n, 0), calloc(0, 0), multiplication overflow rejection, calloc(1, 1)).
- `tests/e2e/tier2_boundary/test_realloc_boundary.py`: 5 tests (realloc(p, 0), realloc(NULL, 0), in-place same size, shrink to 1 byte, overflow size leaves original ptr valid).
- `tests/e2e/tier2_boundary/test_aligned_boundary.py`: 5 tests (rejects non-power-of-two with EINVAL, rejects alignment < sizeof(void*) with EINVAL, aligned_alloc(0) returns NULL, aligned_alloc non-multiple size returns NULL, extended power-of-two alignments [32, 64, 128, 256, 4096]).
  - *Note: `test_05_posix_memalign_extended_power_of_two_alignments` currently fails, precisely capturing Defect M3.3.*
- `tests/e2e/tier2_boundary/test_mmap_boundary.py`: 5 tests (boundary threshold - 1, exact threshold 128KB, sbrk-to-mmap realloc crossing, mmap-to-sbrk realloc crossing, 50-cycle rapid mmap alloc/free).
- `tests/e2e/tier2_boundary/test_coalesce_boundary.py`: 5 tests (coalesce at heap_start boundary, coalesce at heap_end boundary, realloc shrink split coalesce with free neighbour, alternating checkerboard recovery, allocator_destroy / reinit reset).
- `tests/e2e/tier2_boundary/test_canary_doublefree_boundary.py`: 5 tests (realloc UAF fatal abort, header single-bit flip abort, footer single-bit flip abort, corrupted block_size detected, free-list link corruption detected).
- `tests/e2e/tier2_boundary/test_preload_boundary.py`: 5 tests (preload free(NULL), reallocarray overflow rejection with ENOMEM, cross-thread free under preload, rapid subprocess churn, preload environment isolation).
- `tests/e2e/tier2_boundary/test_proc_telemetry_boundary.py`: 5 tests (process CPU% physical sanity bound, memory usage percentage in [0, 100]%, process comm with spaces/brackets parsing, 10 rapid snapshot cycles, snapshot during child process churn).
- `tests/e2e/tier2_boundary/test_cli_telemetry_boundary.py`: 5 tests (pure JSON output on stdout without log pollution, invalid flag code 2, port > 65535 code 2, non-numeric port code 2, missing option value code 2).
  - *Note: `test_01_pure_json_output_no_log_pollution` currently fails, precisely capturing Defect M4.3.*
- `tests/e2e/tier2_boundary/test_http_metrics_boundary.py`: 5 tests (unknown route 404, POST /api/metrics 405/404, 20 concurrent GET queries, dynamic telemetry updates over time, graceful shutdown on SIGINT within 2.0s).
- `tests/e2e/tier2_boundary/test_authenticated_signals_boundary.py`: 5 tests (nonexistent PID error handling, invalid signal name 400, malformed JSON body 400, missing required keys 400, key substring injection strict parsing).

### Tier 3: Cross-Feature Combinations (16 Tests, 100% Pass)
- `test_pairwise_01_mixed_sbrk_and_mmap_cycling`: Interleaved sbrk and mmap allocations and frees.
- `test_pairwise_02_calloc_then_realloc_growth`: Calloc zero verification followed by realloc expansion.
- `test_pairwise_03_realloc_shrink_split_reclaim`: Realloc split remainder immediate reclaim.
- `test_pairwise_04_alloc_free_churn_under_integrity_monitoring`: 1500 operation churn with continuous integrity checks.
- `test_pairwise_05_multithreaded_allocator_under_preload`: 4 concurrent worker threads under LD_PRELOAD.
- `test_pairwise_06_powers_of_two_posix_memalign_lifecycle`: Full alignment lifecycle.
- `test_pairwise_07_aligned_alloc_realloc_lifecycle`: Aligned allocation resized with standard realloc.
- `test_pairwise_08_multithreaded_cross_thread_pass`: Producer/consumer thread cross-thread allocation and deallocation.
- `test_pairwise_09_proc_parser_under_rapid_process_churn`: Proc parser taking snapshots while processes spawn and die.
- `test_pairwise_10_http_metrics_polled_during_memory_churn`: HTTP daemon queried during memory allocator churn.
- `test_pairwise_11_authenticated_signals_concurrent_with_metrics_poll`: Signal dispatch and metric queries concurrently.
- `test_pairwise_12_http_daemon_mixed_traffic_robustness`: Interleaved GET, POST, 404, and static asset traffic.
- `test_pairwise_13_realloc_crossing_mmap_boundary_back_and_forth`: Dual crossing sbrk -> mmap -> sbrk.
- `test_pairwise_14_isolated_canary_abort_during_multithreaded_load`: Subprocess canary abort while parent operates normally.
- `test_pairwise_15_isolated_double_free_during_server_execution`: Subprocess double-free abort while HTTP daemon serves clients.
- `test_pairwise_16_cli_json_captured_under_active_cpu_load`: Headless JSON telemetry captured during CPU compute load.

### Tier 4: Real-World Workloads (5 Scenarios, 100% Pass)
- `test_scenario_01_web_server_simulation`: 8 worker threads handling 50 requests each with dynamic realloc expansion under LD_PRELOAD.
- `test_scenario_02_system_monitoring_rapid_churn`: Real-time monitoring daemon surviving bursts of 30 short-lived processes without latency or crash.
- `test_scenario_03_memory_stress_10000_churn`: 10,000 operation random memory stress test through all segregated size bins and mmap.
- `test_scenario_04_authenticated_remote_task_management`: Full REST workflow: target process spawn, discovery via /api/metrics, authenticated SIGTERM dispatch, termination verification, dead PID handling.
- `test_scenario_05_clean_ld_preload_standard_utilities`: Clean execution of `/bin/ls`, `/bin/grep`, `/bin/cat`, `/bin/sleep`, `/usr/bin/find`, `/usr/bin/env` under `libmyalloc.so`.

---

## Baseline Verification Results
- **Total Tests Run**: 141
- **Passed**: 139 (98.6%)
- **Failed**: 2 (1.4%) — Expected defects matching M3 and M4 milestone deliverables:
  1. `test_05_posix_memalign_extended_power_of_two_alignments` -> Defect M3.3 (Extended alignments > 16 rejected with EINVAL).
  2. `test_01_pure_json_output_no_log_pollution` -> Defect M4.3 (Pure JSON on stdout polluted by LOG_INFO).
- **Sanitizer & Memory Checks**: Zero leaks or corruptions during normal runs.
- **Milestone 6 Target**: Upon landing M3 and M4 fixes, 141 / 141 tests will achieve 100% PASS with exit code 0.
