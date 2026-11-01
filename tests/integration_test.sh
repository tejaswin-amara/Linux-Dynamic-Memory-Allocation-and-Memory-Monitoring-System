#!/usr/bin/env bash
set -euo pipefail

echo "================================================================================"
echo " Running System Integration & End-to-End Tests"
echo "================================================================================"

# 1. Enforce required prerequisites upfront - never silently skip
for req in "libmyalloc.so" "mem_monitor"; do
    if [ ! -f "$req" ]; then
        echo "[-] ERROR: Required prerequisite '$req' is missing." >&2
        echo "    Run 'make all' or 'make test' to compile required binaries." >&2
        exit 1
    fi
done

LOG_FILE="/tmp/mem_monitor_integration_$$.log"
TMP_C="/tmp/allocator_api_test_$$.c"
TMP_BIN="/tmp/allocator_api_test_$$"
TMP_JSON="/tmp/telemetry_test_$$.json"
MON_PID=""

cleanup() {
    local exit_code=$?
    if [ -n "$MON_PID" ] && kill -0 "$MON_PID" 2>/dev/null; then
        kill -INT "$MON_PID" 2>/dev/null || true
        wait "$MON_PID" 2>/dev/null || true
    fi
    if [ "$exit_code" -ne 0 ] && [ -f "$LOG_FILE" ]; then
        echo "[-] mem_monitor integration server log on failure:" >&2
        cat "$LOG_FILE" >&2
    fi
    rm -f "$LOG_FILE" "$TMP_C" "$TMP_BIN" "$TMP_JSON"
    trap - EXIT
    exit "$exit_code"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# 2. Stress workload execution
echo "[+] Testing LD_PRELOAD execution with stress allocation workload..."
bash scripts/stress_test.sh > /dev/null
echo "  [PASS] Stress workload executed successfully."

# 3. Allocator API smoke test
cat > "$TMP_C" <<'EOF'
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
${CC:-cc} -O2 -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE "$TMP_C" -o "$TMP_BIN"
LD_PRELOAD=./libmyalloc.so "$TMP_BIN"
rm -f "$TMP_BIN" "$TMP_C"
echo "  [PASS] LD_PRELOAD aligned-allocation API smoke test."

# 4. mem_monitor headless telemetry snapshot test (UNCONDITIONAL)
echo "[+] Testing mem_monitor headless telemetry snapshot..."
./mem_monitor --headless --json > "$TMP_JSON"
if grep -q "cpu" "$TMP_JSON" && grep -q "mem" "$TMP_JSON"; then
    echo "  [PASS] Valid JSON telemetry generated."
else
    echo "  [FAIL] Invalid JSON output. Captured content was:" >&2
    cat "$TMP_JSON" >&2
    exit 1
fi
rm -f "$TMP_JSON"

# 5. mem_monitor headless HTTP server end-to-end test (UNCONDITIONAL)
echo "[+] Testing mem_monitor headless HTTP server end-to-end..."
PORT=""
for _ in $(seq 1 50); do
    candidate=$(shuf -i 20000-45000 -n 1)
    hex=$(printf '%04X' "$candidate")
    if ! grep -qiE ":${hex} " /proc/net/tcp /proc/net/tcp6 2>/dev/null; then
        PORT="$candidate"
        break
    fi
done
if [ -z "$PORT" ]; then
    echo "[-] ERROR: Could not find an open TCP port." >&2
    exit 1
fi

./mem_monitor --headless --port "$PORT" --token my-test-token > "$LOG_FILE" 2>&1 &
MON_PID=$!

READY=0
for _ in $(seq 1 30); do
    kill -0 "$MON_PID" 2>/dev/null || { echo "[-] ERROR: mem_monitor terminated prematurely during startup." >&2; exit 1; }
    if curl -fsS --connect-timeout 1 --max-time 2 "http://127.0.0.1:${PORT}/api/metrics" >/dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 0.1
done
if [ "$READY" -ne 1 ]; then
    echo "[-] ERROR: mem_monitor HTTP metrics endpoint did not become ready." >&2
    exit 1
fi

METRICS=$(curl -fsS "http://127.0.0.1:${PORT}/api/metrics")
if echo "$METRICS" | grep -q "cpu" && echo "$METRICS" | grep -q "processes"; then
    echo "  [PASS] HTTP GET /api/metrics returned valid telemetry."
else
    echo "  [FAIL] HTTP GET /api/metrics failed to return valid telemetry." >&2
    exit 1
fi

UNAUTH_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:${PORT}/api/process/signal" \
    -H "Content-Type: application/json" -d "{\"pid\": $$, \"signal\": \"0\"}")
if [ "$UNAUTH_STATUS" -eq 401 ]; then
    echo "  [PASS] Unauthenticated signal POST rejected with 401."
else
    echo "  [FAIL] Unauthenticated signal POST was not rejected with 401 (got $UNAUTH_STATUS)." >&2
    exit 1
fi

AUTH_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:${PORT}/api/process/signal" \
    -H "X-Auth-Token: my-test-token" -H "Content-Type: application/json" -d "{\"pid\": $$, \"signal\": \"0\"}")
if [ "$AUTH_STATUS" -eq 200 ]; then
    echo "  [PASS] Authenticated signal POST accepted with 200."
else
    echo "  [FAIL] Authenticated signal POST failed (got $AUTH_STATUS)." >&2
    exit 1
fi

# Clean shutdown check
kill -INT "$MON_PID"
set +e
wait "$MON_PID"
EXIT_CODE=$?
set -e
MON_PID=""

if [ "$EXIT_CODE" -ne 0 ]; then
    echo "[-] ERROR: mem_monitor exited with non-zero code $EXIT_CODE after SIGINT." >&2
    exit 1
fi

echo "================================================================================"
echo " All Integration Tests Passed Successfully."
echo "================================================================================"
