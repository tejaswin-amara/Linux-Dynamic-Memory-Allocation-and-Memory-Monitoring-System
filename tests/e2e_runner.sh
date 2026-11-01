#!/usr/bin/env bash
# ==============================================================================
# Master E2E Test Suite Runner
# Linux Dynamic Memory Allocation & Memory Monitoring System
# KLEF 25CS2104E: Outside-In Operating Systems & Systems Programming
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

echo "================================================================================"
echo " Preparing E2E Environment & Building Artifacts"
echo "================================================================================"

# Ensure shared library and monitor binaries are compiled
if [ ! -f "libmyalloc.so" ] || [ ! -f "mem_monitor" ]; then
    echo "[*] Building core binaries..."
    make all
fi

# Ensure bin directory exists
mkdir -p bin

# Export PYTHONPATH to include project root
export PYTHONPATH="${PROJECT_ROOT}:${PYTHONPATH:-}"

echo "[*] Launching Python 4-tier E2E Test Suite..."
python3 tests/test_e2e_suite.py "$@"
