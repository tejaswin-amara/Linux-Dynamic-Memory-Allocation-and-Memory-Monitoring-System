#!/usr/bin/env bash
set -euo pipefail

SOAK_SECONDS="${SOAK_SECONDS:-20}"
SOAK_TOKEN="${SOAK_TOKEN:-soak-test-token-value}"
LOG_FILE="${TMPDIR:-/tmp}/mem_monitor_selfhosted_soak_$$.log"
MONITOR_PID=""
PORT=""
POST_200_COUNT=0
POST_NON_200_COUNT=0

cleanup() {
  local status=$?
  if [[ -n "$MONITOR_PID" ]] && kill -0 "$MONITOR_PID" 2>/dev/null; then
    kill -INT "$MONITOR_PID" 2>/dev/null || true
    wait "$MONITOR_PID" 2>/dev/null || true
  fi
  rm -f "$LOG_FILE"
  trap - EXIT
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

fail() {
  echo "[-] ERROR: $*" >&2
  if [[ -f "$LOG_FILE" ]]; then
    echo "[+] mem_monitor log:" >&2
    cat "$LOG_FILE" >&2
  fi
  exit 1
}

pick_free_port() {
  local candidate hex
  for _ in $(seq 1 50); do
    candidate=$(shuf -i 20000-45000 -n 1)
    hex=$(printf '%04X' "$candidate")
    if ! grep -qiE ":${hex} " /proc/net/tcp /proc/net/tcp6 2>/dev/null; then
      PORT="$candidate"
      return 0
    fi
  done
  return 1
}

pick_free_port || fail "could not select a free TCP port"

echo "[+] Starting mem_monitor with libmyalloc.so preloaded on port $PORT..."
LD_PRELOAD=./libmyalloc.so ./mem_monitor --headless --port "$PORT" --token "$SOAK_TOKEN" >"$LOG_FILE" 2>&1 &
MONITOR_PID=$!

echo "[+] monitor PID: $MONITOR_PID"
sleep 1

kill -0 "$MONITOR_PID" 2>/dev/null || fail "mem_monitor died during startup"

ALLOCATOR_MAPS=$(grep -c libmyalloc "/proc/$MONITOR_PID/maps" || true)
if (( ALLOCATOR_MAPS <= 0 )); then
  fail "libmyalloc.so is not mapped into mem_monitor (allocator soak would silently test glibc)"
fi
echo "[+] /proc/$MONITOR_PID/maps contains libmyalloc.so ($ALLOCATOR_MAPS mapping(s))"

echo "[+] Waiting for authenticated metrics endpoint..."
READY=0
for _ in $(seq 1 20); do
  kill -0 "$MONITOR_PID" 2>/dev/null || fail "mem_monitor died before the HTTP endpoint became ready"
  if curl -fsS --connect-timeout 1 --max-time 2       "http://127.0.0.1:${PORT}/api/metrics" >/dev/null; then
    READY=1
    break
  fi
  sleep 0.5
done
(( READY == 1 )) || fail "metrics endpoint did not become ready"

sleep 4
kill -0 "$MONITOR_PID" 2>/dev/null || fail "mem_monitor died before the first 5-second RSS sample"

BASELINE_RSS_KB=$(awk '/^VmRSS:/ {print $2}' "/proc/$MONITOR_PID/status")
[[ "$BASELINE_RSS_KB" =~ ^[0-9]+$ ]] || fail "could not read baseline VmRSS"
echo "[+] VmRSS after first 5 s: ${BASELINE_RSS_KB} kB"

echo "[+] Running allocator soak for ${SOAK_SECONDS}s: 8 parallel GETs + authenticated signal-0 POST per iteration..."
END_TIME=$((SECONDS + SOAK_SECONDS))
while (( SECONDS < END_TIME )); do
  kill -0 "$MONITOR_PID" 2>/dev/null || fail "mem_monitor died during the allocator soak"

  if ! seq 8 | xargs -P8 -n1 sh -c       'curl -fsS --connect-timeout 2 --max-time 5 "http://127.0.0.1:$0/api/metrics" >/dev/null'       "$PORT"; then
    fail "one or more concurrent GET /api/metrics requests failed"
  fi

  HTTP_STATUS=$(curl -sS -o /dev/null -w "%{http_code}"     --connect-timeout 2 --max-time 5     -X POST "http://127.0.0.1:${PORT}/api/process/signal"     -H "X-Auth-Token: $SOAK_TOKEN"     -H "Content-Type: application/json"     -d "{\"pid\": $$, \"signal\": \"0\"}") || HTTP_STATUS="000"

  if [[ "$HTTP_STATUS" == "200" ]]; then
    POST_200_COUNT=$((POST_200_COUNT + 1))
  else
    POST_NON_200_COUNT=$((POST_NON_200_COUNT + 1))
    echo "[!] POST /api/process/signal returned HTTP $HTTP_STATUS"
  fi
done

kill -0 "$MONITOR_PID" 2>/dev/null || fail "mem_monitor died before SIGINT shutdown"

FINAL_RSS_KB=$(awk '/^VmRSS:/ {print $2}' "/proc/$MONITOR_PID/status")
[[ "$FINAL_RSS_KB" =~ ^[0-9]+$ ]] || fail "could not read final VmRSS"
RSS_LIMIT_KB=$((BASELINE_RSS_KB * 2))
echo "[+] Final VmRSS: ${FINAL_RSS_KB} kB (2x baseline limit: ${RSS_LIMIT_KB} kB)"

if (( FINAL_RSS_KB > RSS_LIMIT_KB )); then
  fail "final VmRSS exceeded 2x the first-5-second VmRSS baseline"
fi

if (( POST_200_COUNT == 0 )); then
  fail "no authenticated POST /api/process/signal request returned HTTP 200"
fi
echo "[+] Authenticated signal POSTs: ${POST_200_COUNT} HTTP 200, ${POST_NON_200_COUNT} non-200"

if grep -q '\[FATAL\]' "$LOG_FILE"; then
  fail "mem_monitor log contains [FATAL]"
fi

echo "[+] Sending SIGINT to mem_monitor (PID: $MONITOR_PID)..."
kill -INT "$MONITOR_PID"

set +e
wait "$MONITOR_PID"
EXIT_CODE=$?
set -e
MONITOR_PID=""

if (( EXIT_CODE != 0 )); then
  echo "[+] mem_monitor log:" >&2
  cat "$LOG_FILE" >&2
  fail "mem_monitor exited with code $EXIT_CODE after SIGINT"
fi

if grep -q '\[FATAL\]' "$LOG_FILE"; then
  fail "mem_monitor log contains [FATAL]"
fi

echo "[+] Allocator soak completed successfully."
