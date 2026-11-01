#!/usr/bin/env python3
"""
Master E2E Test Suite Runner for Linux Dynamic Memory Allocation & Monitoring System.
Discovers and executes the 4-tier test architecture:
  Tier 1: Feature Coverage (60 tests across 12 feature areas)
  Tier 2: Boundary & Corner Cases (60 tests across 12 feature areas)
  Tier 3: Cross-Feature Combinations (16 pairwise interaction tests)
  Tier 4: Real-World Scenarios (5 complex workload simulations)
Total: 141 comprehensive opaque-box tests.
"""

import argparse
import os
import subprocess
import sys
import time
import unittest

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
E2E_DIR = os.path.join(PROJECT_ROOT, "tests", "e2e")
BIN_DIR = os.path.join(PROJECT_ROOT, "bin")


def build_prerequisites():
    """Build shared library, monitor binary, and C helper binaries."""
    os.makedirs(BIN_DIR, exist_ok=True)
    if not os.path.isfile(os.path.join(PROJECT_ROOT, "libmyalloc.so")) or not os.path.isfile(
        os.path.join(PROJECT_ROOT, "mem_monitor")
    ):
        print("==> Building primary binaries (libmyalloc.so, mem_monitor)...")
        res = subprocess.run(["make", "all"], cwd=PROJECT_ROOT, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[ERROR] make all failed:\n{res.stderr}", file=sys.stderr)
            sys.exit(1)

    # Build C helpers
    helpers = [
        (
            os.path.join(BIN_DIR, "e2e_alloc_helper"),
            [
                "gcc",
                "-Wall",
                "-Wextra",
                "-std=c11",
                "-D_GNU_SOURCE",
                f"-I{os.path.join(PROJECT_ROOT, 'include')}",
                f"-I{os.path.join(PROJECT_ROOT, 'src', 'allocator')}",
                os.path.join(E2E_DIR, "helpers", "e2e_alloc_helper.c"),
                os.path.join(PROJECT_ROOT, "src", "allocator", "allocator.c"),
                os.path.join(PROJECT_ROOT, "src", "allocator", "free_list.c"),
                "-o",
                os.path.join(BIN_DIR, "e2e_alloc_helper"),
                "-pthread",
            ],
        ),
        (
            os.path.join(BIN_DIR, "e2e_proc_helper"),
            [
                "gcc",
                "-Wall",
                "-Wextra",
                "-std=c11",
                "-D_GNU_SOURCE",
                f"-I{os.path.join(PROJECT_ROOT, 'include')}",
                os.path.join(PROJECT_ROOT, "src", "monitor", "proc_parser.c"),
                os.path.join(E2E_DIR, "helpers", "e2e_proc_helper.c"),
                "-o",
                os.path.join(BIN_DIR, "e2e_proc_helper"),
                "-lm",
            ],
        ),
        (
            os.path.join(BIN_DIR, "e2e_thread_workload"),
            [
                "gcc",
                "-Wall",
                "-Wextra",
                "-std=c11",
                "-D_GNU_SOURCE",
                os.path.join(E2E_DIR, "helpers", "e2e_thread_workload.c"),
                "-o",
                os.path.join(BIN_DIR, "e2e_thread_workload"),
                "-pthread",
            ],
        ),
    ]

    for target_bin, compile_cmd in helpers:
        if not os.path.isfile(target_bin):
            res = subprocess.run(compile_cmd, cwd=PROJECT_ROOT, capture_output=True, text=True)
            if res.returncode != 0:
                print(f"[ERROR] Failed to compile helper {target_bin}:\n{res.stderr}", file=sys.stderr)
                sys.exit(1)


def load_suite(tiers=None, filter_pattern=None):
    """Load test suites for selected tiers and optional pattern."""
    loader = unittest.TestLoader()
    master_suite = unittest.TestSuite()

    tier_dirs = {
        1: ("tier1_feature", "Tier 1: Feature Coverage"),
        2: ("tier2_boundary", "Tier 2: Boundary & Corner Cases"),
        3: ("tier3_cross_feature", "Tier 3: Cross-Feature Combinations"),
        4: ("tier4_real_world", "Tier 4: Real-World Scenarios"),
    }

    selected_tiers = tiers if tiers else [1, 2, 3, 4]

    for t in selected_tiers:
        if t in tier_dirs:
            dirname, desc = tier_dirs[t]
            start_dir = os.path.join(E2E_DIR, dirname)
            if os.path.isdir(start_dir):
                suite = loader.discover(
                    start_dir=start_dir,
                    pattern="test_*.py" if not filter_pattern else filter_pattern,
                    top_level_dir=PROJECT_ROOT,
                )
                master_suite.addTest(suite)

    return master_suite


def main():
    parser = argparse.ArgumentParser(description="Linux Allocator & Monitor E2E Test Suite")
    parser.add_argument(
        "--tier",
        type=int,
        choices=[1, 2, 3, 4],
        action="append",
        help="Specific tier to run (1, 2, 3, or 4). Default: all tiers.",
    )
    parser.add_argument(
        "--filter",
        type=str,
        default=None,
        help="Pattern to filter test files (e.g. test_malloc*).",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Verbose test execution output.",
    )
    args = parser.parse_args()

    # Ensure project root is on PYTHONPATH
    if PROJECT_ROOT not in sys.path:
        sys.path.insert(0, PROJECT_ROOT)

    print("================================================================================")
    print(" Linux Dynamic Memory Allocator & Monitoring System: 4-Tier E2E Test Suite")
    print("================================================================================")

    build_prerequisites()

    suite = load_suite(tiers=args.tier, filter_pattern=args.filter)
    total_count = suite.countTestCases()
    print(f"[*] Discovered {total_count} test cases across requested tiers.")

    start_time = time.time()
    runner = unittest.TextTestRunner(
        verbosity=2 if args.verbose else 1,
        failfast=False,
    )
    result = runner.run(suite)
    elapsed = time.time() - start_time

    print("\n================================================================================")
    print(" E2E Test Execution Summary")
    print("================================================================================")
    print(f"Total Tests Run : {result.testsRun}")
    print(f"Passed          : {result.testsRun - len(result.failures) - len(result.errors)}")
    print(f"Failures        : {len(result.failures)}")
    print(f"Errors          : {len(result.errors)}")
    print(f"Execution Time  : {elapsed:.2f}s")
    print("================================================================================")

    if result.wasSuccessful():
        print(" [PASS] All test assertions verified successfully.")
        return 0
    else:
        print(f" [FAIL] {len(result.failures) + len(result.errors)} tests failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
