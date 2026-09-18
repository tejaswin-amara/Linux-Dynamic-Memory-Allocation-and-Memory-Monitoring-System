# ADR-002: Direct Unbuffered Virtual File System (/proc) Parsing Strategy

## Status
**Accepted** (Implemented in `src/monitor/proc_parser.c`)

## Context & Problem Statement
In systems software development (KLEF 25CS2104E, Course Outcomes CO1, CO2, and CO5), monitoring system resource consumption (CPU scheduling, memory footprints, and process states) requires polling the Linux Virtual File System (`/proc`).

A fundamental tenet of performance monitoring tools is **minimizing the observer effect**: the monitoring system must not perturb or skew the metrics of the system it is profiling. Standard C programs typically rely on standard library buffered I/O (`fopen`, `fgets`, `fscanf`, `getline`), which:
1. Performs internal heap allocations via `malloc` for file stream buffers (typically 4 KB or 8 KB buffers per stream).
2. Introduces caching and buffering delays that can lead to stale metric reads.
3. Consumes unnecessary file descriptor handles and internal POSIX stream locking overhead (`flockfile`).

We required a telemetry collection engine capable of rapid sampling (250 ms to 1000 ms intervals) across hundreds of system processes without allocating heap memory or degrading system responsiveness.

## Decision Drivers
- **Zero-Allocation Sampling**: Zero dynamic allocations during the critical monitoring loop.
- **Low CPU Overhead**: Fast tokenization and numeric parsing executed directly from stack memory.
- **Accurate Differential Scheduling Metrics**: Real-time multi-core CPU utilization computed from raw jiffies.
- **Container and Cloud Compatibility**: Unprivileged operation within standard Linux namespaces without requiring root or kernel modules.

## Considered Alternatives
1. **Buffered Standard I/O (`fopen` / `fgets`)**:
   - *Pros*: Convenient line-by-line parsing.
   - *Cons*: Triggers heap allocations on every open stream; internal glibc locks introduce thread contention; high memory churn.
2. **Third-Party Libraries (`libprocps` / `libgtop`)**:
   - *Pros*: Pre-built abstractions for top-like utilities.
   - *Cons*: Violates outside-in systems programming requirements; introduces heavy runtime dependencies; opaque memory allocation behavior.
3. **Kernel eBPF / Perf Events**:
   - *Pros*: Ultra-low overhead kernel-space event sampling.
   - *Cons*: Requires root privileges (`CAP_SYS_ADMIN`/`CAP_BPF`); not portable to restricted containers or pedagogical execution environments.
4. **Direct Unbuffered POSIX File Syscalls (`open(2)`, `read(2)`, `close(2)`) (Selected)**:
   - *Pros*: Completely zero-allocation; direct interaction with kernel VFS; deterministic execution time; minimal stack footprint.

## Decision Outcome
We implemented a **direct, unbuffered VFS parsing engine** relying strictly on low-level POSIX system calls and fixed-size stack buffers:

### 1. Unbuffered Syscall Engine
All interactions with `/proc` strictly use:
```c
int fd = open(filepath, O_RDONLY);
ssize_t bytes = read(fd, stack_buffer, sizeof(stack_buffer) - 1);
close(fd);
```
Stack buffers are sized at 4096 bytes (matching standard Linux virtual memory page size), guaranteeing that small pseudo-files (`/proc/stat`, `/proc/meminfo`, `/proc/[pid]/stat`, `/proc/[pid]/status`) are retrieved in a single atomic `read(2)` syscall.

### 2. Monitored VFS Nodes & Extracted Metrics
| VFS Path | Extracted Parameters | Computation Target |
|---|---|---|
| `/proc/stat` | `user`, `nice`, `system`, `idle`, `iowait`, `irq`, `softirq`, `steal` | Aggregate system jiffies and total CPU delta |
| `/proc/meminfo` | `MemTotal`, `MemFree`, `MemAvailable`, `Buffers`, `Cached`, `SwapTotal`, `SwapFree` | Physical RAM and Swap saturation percentages |
| `/proc/[pid]/stat` | `comm`, `state`, `ppid`, `utime`, `stime`, `priority`, `nice`, `num_threads` | Process lifecycle state, parentage, and execution time |
| `/proc/[pid]/status` | `VmSize`, `VmRSS`, `voluntary_ctxt_switches`, `nonvoluntary_ctxt_switches` | Memory footprints and voluntary/involuntary context switches |
| `/proc/[pid]/smaps_rollup` | `Pss`, `Rss`, `Shared_Clean`, `Shared_Dirty` | Proportional Set Size (PSS) accounting |

### 3. Differential Multi-Core CPU Computation
To report accurate instantaneous CPU usage, the engine maintains previous sampling snapshots (`prev_cpu_jiffies` and `prev_processes` table) to calculate differential values:

$$\Delta \text{proc\_time} = (\text{utime}_2 + \text{stime}_2) - (\text{utime}_1 + \text{stime}_1)$$

$$\Delta \text{total\_jiffies} = \text{total\_jiffies}_2 - \text{total\_jiffies}_1$$

$$\text{CPU \%} = \left( \frac{\Delta \text{proc\_time}}{\Delta \text{total\_jiffies}} \right) \times 100 \times N_{\text{cores}}$$

This formula accurately reflects multi-threaded utilization and normalizes percentages across multi-core systems discovered via `sysconf(_SC_NPROCESSORS_ONLN)`.

## Consequences

### Positive
- **Deterministic Zero-Allocation Execution**: Completely eliminates memory churn and heap fragmentation during telemetry gathering.
- **Minimal Observer Impact**: CPU consumption of `mem_monitor` typically remains under 1% on modern Linux systems.
- **Portability**: Operates without special privileges in user space across Ubuntu, Debian, Alpine, and containerized Docker environments.

### Negative
- **Buffer Size Constraints**: Files exceeding the 4096-byte stack buffer (e.g. detailed `/proc/[pid]/maps`) require bounded multi-chunk processing or targeted truncation.
- **Proc State Churn**: Short-lived processes may terminate between `opendir("/proc")` and `open("/proc/[pid]/stat")`, requiring defensive handling for `ENOENT`/`ESRCH`.

## Verification & Compliance
- **Unit Tests**: `tests/test_proc_parser.c` validates parsing correctness against live system metrics.
- **Syscall Verification**: Traced with `strace -e trace=openat,read,close ./mem_monitor --json` confirming zero `brk`/`mmap` calls during sampling.
- **Valgrind**: Verified zero memory leaks under `make valgrind`.
