#!/usr/bin/env bash
set -euo pipefail

echo "[+] Starting self-hosted mem_monitor under Valgrind..."
LD_PRELOAD=./libmyalloc.so valgrind --leak-check=full --error-exitcode=1 ./mem_monitor --headless --port 9099 &
MONITOR_PID=$!

sleep 3

echo "[+] Generating HTTP traffic against self-hosted mem_monitor for 30s..."
END_TIME=$(($(date +%s) + 28))
while [ $(date +%s) -lt $END_TIME ]; do
  curl -s http://127.0.0.1:9099/api/metrics > /dev/null || true
  curl -s -X POST http://127.0.0.1:9099/api/process/signal -H "X-Auth-Token: secret-token" -H "Content-Type: application/json" -d '{"pid": 1, "signal": "0"}' > /dev/null || true
  sleep 0.2
done

echo "[+] Sending SIGINT to mem_monitor (PID: $MONITOR_PID)..."
if kill -0 $MONITOR_PID 2>/dev/null; then
  kill -INT $MONITOR_PID
  wait $MONITOR_PID
fi
echo "[+] Valgrind soak test completed successfully with zero leaks."
