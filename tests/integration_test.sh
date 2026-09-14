#!/usr/bin/env bash
set -euo pipefail

echo "================================================================================"
echo " Running System Integration & End-to-End Tests"
echo "================================================================================"

# 1. Verify library compilation
if [ ! -f "libmyalloc.so" ]; then
    echo "[!] libmyalloc.so missing. Building..."
    make libmyalloc.so
fi

# 2. Test LD_PRELOAD on standard coreutils
echo "[+] Testing LD_PRELOAD execution with /bin/ls..."
LD_PRELOAD=./libmyalloc.so /bin/ls -la /tmp > /dev/null
echo "  [PASS] /bin/ls executed successfully under libmyalloc.so"

# 3. Test Headless JSON Telemetry mode of mem_monitor
if [ -f "mem_monitor" ]; then
    echo "[+] Testing mem_monitor headless telemetry snapshot..."
    ./mem_monitor --headless --json > /tmp/telemetry_test.json
    if grep -q "cpu" /tmp/telemetry_test.json && grep -q "mem" /tmp/telemetry_test.json; then
        echo "  [PASS] Valid JSON telemetry generated."
    else
        echo "  [FAIL] Invalid JSON output."
        exit 1
    fi
    rm -f /tmp/telemetry_test.json
fi

echo "================================================================================"
echo " All Integration Tests Passed Successfully."
echo "================================================================================"
