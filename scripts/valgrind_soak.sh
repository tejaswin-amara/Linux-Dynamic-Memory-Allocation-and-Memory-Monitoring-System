#!/usr/bin/env bash
set -euo pipefail

SOAK_TOKEN="soak-test-token-value"
SUCCESS_200_COUNT=0

echo "[+] Starting mem_monitor under Valgrind with token '$SOAK_TOKEN'..."
valgrind --leak-check=full --error-exitcode=1 ./mem_monitor --headless --port 9099 --token "$SOAK_TOKEN" &
MONITOR_PID=$!

sleep 3
echo "[+] Generating HTTP traffic against self-hosted mem_monitor for 30s..."
END_TIME=$(($(date +%s) + 28))
while [ $(date +%s) -lt "$END_TIME" ]; do
  curl -s http://127.0.0.1:9099/api/metrics > /dev/null || true

  HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:9099/api/process/signal \
    -H "X-Auth-Token: $SOAK_TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"pid\": $$, \"signal\": \"0\"}") || true

  if [ "$HTTP_STATUS" = "200" ]; then
    SUCCESS_200_COUNT=$((SUCCESS_200_COUNT + 1))
    echo "[+] POST /api/process/signal returned HTTP 200 OK (count: $SUCCESS_200_COUNT)"
  fi
  sleep 0.2
done

echo "[+] Sending SIGINT to mem_monitor (PID: $MONITOR_PID)..."
if kill -0 "$MONITOR_PID" 2>/dev/null; then
  kill -INT "$MONITOR_PID"
  set +e
  wait "$MONITOR_PID"
  EXIT_CODE=$?
  set -e
else
  echo "[-] ERROR: mem_monitor died before SIGINT" >&2
  exit 1
fi

echo "[+] Total successful 200 OK signal responses: $SUCCESS_200_COUNT"
if [ "$SUCCESS_200_COUNT" -lt 1 ]; then
  echo "[-] ERROR: Zero POST /api/process/signal requests returned 200 OK!"
  exit 1
fi

if [ "$EXIT_CODE" -ne 0 ]; then
  echo "[-] ERROR: mem_monitor exited with code $EXIT_CODE after SIGINT!"
  exit 1
fi

echo "[+] Valgrind soak test completed successfully with zero leaks."
