#!/usr/bin/env bash
set -euo pipefail

echo "================================================================================"
echo " Running System Integration & End-to-End Tests"
echo "================================================================================"

if [ ! -f "libmyalloc.so" ]; then
    echo "[!] libmyalloc.so missing. Building..."
    make libmyalloc.so
fi

echo "[+] Testing LD_PRELOAD execution with stress allocation workload..."
bash scripts/stress_test.sh > /dev/null
echo "  [PASS] Stress workload executed successfully."
cat > /tmp/allocator_api_test.c <<'EOF'
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    void *p = NULL;
    if (posix_memalign(&p, 16, 128) != 0 || !p || ((uintptr_t)p % 16) != 0) return 1;
    free(p);

    p = aligned_alloc(16, 128);
    if (!p || ((uintptr_t)p % 16) != 0) return 1;
    free(p);

    int *items = reallocarray(NULL, 16, sizeof(*items));
    if (!items) return 1;
    for (int i = 0; i < 16; ++i) items[i] = i;
    free(items);

    if (posix_memalign(&p, 3, 64) != EINVAL) return 1;
    return 0;
}
EOF
gcc -O2 -Wall -Wextra -Werror -std=c11 /tmp/allocator_api_test.c -o /tmp/allocator_api_test
LD_PRELOAD=./libmyalloc.so /tmp/allocator_api_test
rm -f /tmp/allocator_api_test /tmp/allocator_api_test.c
echo "  [PASS] LD_PRELOAD aligned-allocation API smoke test."

if [ -f "mem_monitor" ]; then
    echo "[+] Testing mem_monitor headless telemetry snapshot..."
    ./mem_monitor --headless --json > /tmp/telemetry_test.json
    if grep -q "cpu" /tmp/telemetry_test.json && grep -q "mem" /tmp/telemetry_test.json; then
        echo "  [PASS] Valid JSON telemetry generated."
    else
        echo "  [FAIL] Invalid JSON output."
        false
    fi
    rm -f /tmp/telemetry_test.json
fi

if [ -f "mem_monitor" ]; then
    echo "[+] Testing mem_monitor headless HTTP server end-to-end..."
    PORT="$(shuf -i 20000-45000 -n 1)"
    ./mem_monitor --headless --port "$PORT" --token my-test-token > /tmp/mem_monitor_integration.log 2>&1 &
    MON_PID=$!
    trap 'kill -INT "$MON_PID" 2>/dev/null || true; wait "$MON_PID" 2>/dev/null || true; rm -f /tmp/mem_monitor_integration.log' EXIT
    sleep 1

    kill -0 "$MON_PID" 2>/dev/null
    METRICS=$(curl -fsS "http://127.0.0.1:${PORT}/api/metrics")
    if echo "$METRICS" | grep -q "cpu" && echo "$METRICS" | grep -q "processes"; then
        echo "  [PASS] HTTP GET /api/metrics returned valid telemetry."
    else
        echo "  [FAIL] HTTP GET /api/metrics failed."
        kill -9 $MON_PID 2>/dev/null || true
        false
    fi

    UNAUTH_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:${PORT}/api/process/signal -H "Content-Type: application/json" -d "{\"pid\": $$, \"signal\": \"0\"}")
    if [ "$UNAUTH_STATUS" -eq 401 ]; then
        echo "  [PASS] Unauthenticated signal POST rejected with 401."
    else
        echo "  [FAIL] Unauthenticated signal POST was not rejected with 401 (got $UNAUTH_STATUS)."
        kill -9 $MON_PID 2>/dev/null || true
        false
    fi

    AUTH_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:9088/api/process/signal -H "X-Auth-Token: my-test-token" -H "Content-Type: application/json" -d "{\"pid\": $$, \"signal\": \"0\"}")
    if [ "$AUTH_STATUS" -eq 200 ]; then
        echo "  [PASS] Authenticated signal POST accepted with 200."
    else
        echo "  [FAIL] Authenticated signal POST failed (got $AUTH_STATUS)."
        kill -9 $MON_PID 2>/dev/null || true
        false
    fi

    kill -INT "$MON_PID" 2>/dev/null || true
    wait "$MON_PID" 2>/dev/null || true
    trap - EXIT
    rm -f /tmp/mem_monitor_integration.log
fi

echo "================================================================================"
echo " All Integration Tests Passed Successfully."
echo "================================================================================"
