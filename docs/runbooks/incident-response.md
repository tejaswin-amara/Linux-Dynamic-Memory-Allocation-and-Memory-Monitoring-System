# Runbook: Incident Response & Process Mitigation

## Purpose
This runbook guides operators on mitigating rogue processes, memory exhaustion, and high CPU anomalies using `mem_monitor`.

## Scenarios & Procedures

### 1. High Memory Consumption / OOM Pressure
1. **Identify Target**:
   - In the TUI, press `m` to sort processes by Memory (RSS) descending.
   - Or open `http://localhost:8080` and click the "RSS" table header.
2. **Inspect Process Details**:
   - Note PID and executable path from table.
   - Check `/proc/<PID>/smaps_rollup` to distinguish between Shared vs Private memory.
3. **Graceful Termination**:
   - In TUI, navigate with arrow keys to the PID, press `k`, then enter `15` (`SIGTERM`).
   - If uncooperative after 5 seconds, press `k` and enter `9` (`SIGKILL`).

### 2. High CPU Utilization / Runaway Threads
1. **Sort by CPU**:
   - Press `p` in TUI to sort by CPU% descending.
2. **Temporary Throttling / Pausing**:
   - Send `SIGSTOP` by pressing `s` to pause execution without killing the process.
   - Investigate process stack using `gdb -p <PID>` or `strace -p <PID>`.
3. **Resume Execution**:
   - Send `SIGCONT` by pressing `c` to resume normal operation.
