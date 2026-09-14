# ADR-002: Direct Unbuffered Virtual File System (/proc) Parsing Strategy

## Status
Accepted

## Context
Process monitoring and virtual memory inspection in Linux must be performed with minimum overhead without perturbing the target system's performance. Relying on heavy abstractions or buffered glibc file streams (`fopen`/`fgets`) introduces unwanted heap allocations, internal caching delays, and file descriptor overhead.

## Decision
1. **Direct Syscalls**:
   - Strictly utilize low-level POSIX file I/O: `open(2)`, `read(2)`, and `close(2)` with `O_RDONLY`.
   - Read unbuffered chunks directly into pre-allocated stack/static buffers (e.g., 4096-byte buffers), avoiding dynamic allocation during the critical metrics collection loop.
2. **Target VFS Nodes**:
   - `/proc/stat`: Aggregate CPU jiffies across all states (user, nice, system, idle, iowait, irq, softirq, steal) to compute total system time.
   - `/proc/meminfo`: Extract MemTotal, MemFree, MemAvailable, Buffers, Cached, SwapTotal, SwapFree.
   - `/proc/[pid]/stat`: Tokenize process state, parent PID, utime, stime, priority, and thread count.
   - `/proc/[pid]/status`: Extract VmSize, VmRSS, voluntary/nonvoluntary context switches.
   - `/proc/[pid]/maps` & `/proc/[pid]/smaps_rollup`: Extract virtual memory segment boundaries, permissions, and PSS/RSS mapping.
3. **Differential Metrics Computation**:
   - Maintain historical state across polling intervals ($\Delta t$) to compute genuine differential CPU percentages:
     $$\Delta \text{proc} = (utime_2 + stime_2) - (utime_1 + stime_1)$$
     $$\Delta \text{total} = \text{total\_jiffies}_2 - \text{total\_jiffies}_1$$
     $$\text{CPU \%} = \left(\frac{\Delta \text{proc}}{\Delta \text{total}}\right) \times 100 \times \text{cores}$$

## Consequences
- Completely zero-allocation telemetry parsing in the monitoring loop.
- Full compatibility with restricted or containerized Linux environments.
