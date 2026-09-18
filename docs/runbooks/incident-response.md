# Runbook: Systems Incident Response & Process Mitigation

## Purpose & Scope
This operational runbook provides systems administrators and operators with standardized procedures for diagnosing, throttling, and mitigating rogue user processes, severe CPU saturation, Out-Of-Memory (OOM) pressure, and anomalous process states using the **`mem_monitor`** telemetry suite.

---

## 1. Incident Severity Matrix

| Severity | Condition | Primary Indicator | Immediate Action |
|:---:|---|---|---|
| **P1 - Critical** | System-wide OOM or kernel lockup | Swap utilization $> 90\%$, MemAvailable $< 5\%$ | Identify top RSS consumer, dispatch `SIGKILL` (<kbd>Shift+K</kbd>) |
| **P2 - High** | Runaway process saturating CPU cores | CPU Utilization $> 90\%$ sustained | Pause execution via `SIGSTOP` (<kbd>s</kbd>), profile with `gdb`/`strace` |
| **P3 - Medium** | High fork rate or accumulating zombies | Process count $> 1000$, multiple state `Z` | Trace parent PID (`ppid`), terminate parent process |
| **P4 - Low** | Telemetry server port collision | `mem_monitor` fails to bind on port 8080 | Restart on alternative port via `--port <PORT>` |

---

## 2. Mitigation Procedures

### Scenario A: Severe Memory Exhaustion / OOM Pressure

#### 1. Rapid Identification
- **Via Terminal UI (TUI)**:
  - Launch or switch to `mem_monitor`.
  - Press <kbd>m</kbd> or <kbd>M</kbd> to sort the process table by **Resident Set Size (RSS)** descending.
  - The highest memory-consuming process is automatically highlighted at the top of the list.
- **Via Web GUI**:
  - Navigate to `http://localhost:8080`.
  - Select **Sort by Memory (RSS)** from the dropdown control.

#### 2. Memory Deep-Dive Inspection
Before terminating the target process, capture memory structure data:
```bash
# Inspect proportional vs private memory breakdown
cat /proc/<PID>/smaps_rollup | grep -E "Rss|Pss|Shared_Dirty|Private_Dirty"

# Check virtual memory map layout
cat /proc/<PID>/maps | head -n 30
```

#### 3. Execution Termination Ladder
- **Step 1 (Graceful Pause)**:
  - In the TUI, select the process and press <kbd>s</kbd> to dispatch `SIGSTOP`. This freezes memory allocation activity without terminating the process, stabilizing the system break point.
- **Step 2 (Graceful Termination)**:
  - In the Web GUI, click the process's **[STOP]** or dispatch `SIGTERM` via:
    ```bash
    curl -X POST http://localhost:8080/api/process/signal \
         -H "Content-Type: application/json" \
         -d '{"pid": <PID>, "signal": "SIGTERM"}'
    ```
- **Step 3 (Forced Kill)**:
  - If the process fails to terminate within 5 seconds, press <kbd>K</kbd> *(Shift+K)* in the TUI, or click **[KILL]** in the Web GUI modal to issue an uncatchable `SIGKILL` (signal 9).

---

### Scenario B: Sustained High CPU Saturation / Runaway Loops

#### 1. Detection & Sorting
- In the TUI, press <kbd>p</kbd> or <kbd>P</kbd> to sort processes by **differential CPU %** descending.
- Note the process PID, thread count (`THREADS`), and scheduling state (`S`).

#### 2. Non-Destructive Throttling
1. Navigate cursor to the offending PID (<kbd>j</kbd>/<kbd>k</kbd> or <kbd>↓</kbd>/<kbd>↑</kbd>).
2. Press <kbd>s</kbd> to send `SIGSTOP`.
3. Verify that the global CPU meter in the header drops immediately.

#### 3. Root Cause Profiling
While the process is suspended, inspect its thread call stacks:
```bash
# Sample system call activity
strace -p <PID> -c

# Attach GDB and dump backtrace across all threads
gdb -p <PID> -batch -ex "thread apply all bt"
```

#### 4. Resuming or Clearing
- If execution should continue: Press <kbd>c</kbd> in the TUI to send `SIGCONT`.
- If the workload is invalid: Press <kbd>K</kbd> to terminate.

---

### Scenario C: Zombie (`Z`) and Uninterruptible (`D`) Process States

#### 1. Zombie Processes (`State: Z`)
- **Diagnosis**: A process whose execution is complete, but whose parent has not called `wait(2)` or `waitpid(2)`.
- **Mitigation**: A zombie cannot be killed via `SIGKILL` because it is already dead.
  1. Identify the parent PID from the `PPID` column in `mem_monitor`.
  2. Inspect the parent process. If the parent is hung, terminate the parent process:
     ```bash
     kill -TERM <PPID>
     ```
  3. Once the parent terminates, the zombie is reparented to PID 1 (`systemd`), which immediately reaps it.

#### 2. Uninterruptible Sleep (`State: D`)
- **Diagnosis**: The process is blocked waiting for kernel I/O (e.g. disk read, stalled NFS mount, device driver lock).
- **Mitigation**: Processes in state `D` do not process signals until the I/O request returns. Inspect kernel wait channels:
  ```bash
  cat /proc/<PID>/wchan
  cat /proc/<PID>/stack
  ```

---

### Scenario D: Telemetry Daemon Port Collision

If `mem_monitor` fails to start with:
`[ERROR] Failed to bind HTTP socket to port 8080`

1. Identify the competing process:
   ```bash
   lsof -i :8080
   # or
   ss -tulpn | grep 8080
   ```
2. Launch `mem_monitor` on an alternative free port:
   ```bash
   ./mem_monitor --port 9090
   ```
3. Or run in headless single-shot JSON mode:
   ```bash
   ./mem_monitor --json
   ```
