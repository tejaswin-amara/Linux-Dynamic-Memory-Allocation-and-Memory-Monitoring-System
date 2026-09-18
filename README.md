# Linux Dynamic Memory Allocation & System Task Manager (CLI + Web/GUI)

[![Systems CI Pipeline](https://github.com/tejaswin-amara/Linux-Dynamic-Memory-Allocation-and-Memory-Monitoring-System/actions/workflows/ci.yml/badge.svg)](https://github.com/tejaswin-amara/Linux-Dynamic-Memory-Allocation-and-Memory-Monitoring-System/actions/workflows/ci.yml)
[![Standard](https://img.shields.io/badge/standard-C11%20%7C%20POSIX.1--2008-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A production-grade Systems Software Capstone engineering project developed for **KLEF 25CS2104E: Outside-In Operating Systems & Systems Programming**. The system unites a custom high-performance dynamic memory allocator (`libmyalloc.so`) with an unbuffered Virtual File System process monitor (`mem_monitor`) featuring dual interfaces: an interactive `ncurses` Terminal User Interface (TUI) and an embedded C HTTP JSON telemetry server for a live Web GUI.

---

## 1. Curriculum Traceability Matrix (25CS2104E)

| Course Outcome | Description | Concrete Implementation in Codebase |
|---|---|---|
| **CO1: OS Service Layer** | Trace & utilize low-level syscalls interacting at kernel boundaries | `sbrk(2)`, `mmap(2)`, `munmap(2)`, `open(2)`, `read(2)`, `close(2)`, `kill(2)`, `sysconf(3)`. Strict validation against `errno` with `strace` verification. |
| **CO2: Process Control** | Process lifecycle, daemonization, states, and worker threads | Scanning `/proc/[pid]/stat`, parsing states (`R`, `S`, `D`, `Z`, `T`), calculating differential CPU times, and managing background telemetry workers. |
| **CO3: Inter-Process Communication** | Signal dispatching and telemetry streaming | Real-time delivery of POSIX signals (`SIGINT`, `SIGTERM`, `SIGKILL`, `SIGSTOP`, `SIGCONT`) via `kill()`; streaming JSON metrics via POSIX TCP sockets. |
| **CO4: Memory Management** | Custom dynamic allocation & virtual memory mapping | Segregated free lists (10 size bins), boundary tags with `0xDEADBEEF` canaries, hybrid `sbrk()` (<128 KB) vs `mmap()` (>=128 KB), and `/proc/[pid]/maps` parsing. |
| **CO5: File Systems** | Direct VFS parsing via low-level file I/O | Unbuffered `open()`, `read()`, and `close()` parsing of `/proc/stat`, `/proc/meminfo`, `/proc/[pid]/status`, and `/proc/[pid]/smaps_rollup`. |
| **CO6: Concurrency** | POSIX threading and synchronization | Multi-threaded heap synchronization (`pthread_mutex_t`) in allocator; asynchronous sampling threads and read-write locks (`pthread_rwlock_t`) in GUI server. |

---

## 2. System Architecture

```mermaid
graph TD
    subgraph User Application Runtime
        APP[Target Application / Process] -->|LD_PRELOAD| SHIM[preload_shim.c]
        SHIM --> ALLOC[my_malloc / my_free]
        ALLOC --> SEGLIST[Segregated Free Lists<br/>10 Size Classes]
        ALLOC -->|size < 128KB| SBRK[sbrk Syscall]
        ALLOC -->|size >= 128KB| MMAP[mmap Syscall]
    end

    subgraph Kernel Space
        SBRK --> HEAP[(Heap Segment)]
        MMAP --> ANONYMOUS[(Anonymous Pages)]
        VFS[(Linux /proc VFS)]
    end

    subgraph Task Manager Daemon [mem_monitor]
        PARSER[proc_parser.c<br/>Unbuffered VFS Engine] -->|"Reads /proc/stat, meminfo, [pid]"| VFS
        SIGNAL[signal_handler.c] -->|kill(pid, sig)| APP
        SNAPSHOT[(System Telemetry Snapshot)]
        PARSER --> SNAPSHOT
        SNAPSHOT --> TUI[tui.c<br/>ncurses Dashboard]
        SNAPSHOT --> SERVER[gui_server.c<br/>Embedded HTTP Server]
        SERVER -->|JSON /api/metrics| WEB[Web GUI Dashboard<br/>http://localhost:8080]
    end
```

---

## 3. Core Modules

### 3.1 Custom Dynamic Memory Allocator (`libmyalloc.so`)
- **Segregated Free Lists**: Ten discrete size bins minimizing external and internal fragmentation.
- **Canary Boundary Tags**: Header (`0xDEADBEEF`) and footer (`0xBEEFDEAD`) block descriptors for instant detection of buffer overruns and double-free anomalies.
- **Coalescing**: Automatic bidirectional merging of adjacent free blocks upon `my_free()`.
- **Preload Shim**: Drop-in runtime interception for arbitrary Linux binaries using `LD_PRELOAD=./libmyalloc.so`.

### 3.2 Process & VFS Monitor (`mem_monitor`)
- **Zero-Allocation Sampling Loop**: Uses pre-allocated stack buffers and direct POSIX `read(2)` calls.
- **Accurate CPU Differentials**:

$$\text{CPU \%} = \left(\frac{(utime_2 + stime_2) - (utime_1 + stime_1)}{\text{total\_jiffies}_2 - \text{total\_jiffies}_1}\right) \times 100 \times \text{cores}$$

- **Virtual Memory Rollup**: Computes RSS, PSS, and VSize directly from `/proc/[pid]/stat` and `/proc/[pid]/smaps_rollup`.
### 3.3 Dual Interface Layer
- **TUI (CLI)**: Built with `ncurses`. Non-blocking keyboard navigation (`nodelay`). Supports sort toggles (`p` for CPU, `m` for Memory) and real-time signal dispatch (`k`, `s`, `c`).
- **Web GUI**: Multi-threaded embedded C HTTP daemon serving an HTML5/CSS3 dark-mode dashboard with live canvas meters and signal control buttons.

---

## 4. Build and Verification Matrix

### Prerequisites
Ubuntu 24.04 LTS or compatible Linux environment with:
```bash
sudo apt-get update
sudo apt-get install -y build-essential gcc make valgrind libncurses-dev clang-format
```

### Build Commands
```bash
# Build libmyalloc.so and mem_monitor binary
make all

# Run Unity unit tests and integration test suite
make test

# Build with AddressSanitizer and UndefinedBehaviorSanitizer
make asan

# Verify memory cleanliness under Valgrind
make valgrind

# Benchmark allocator performance against glibc
make benchmark
```

---

## 5. Usage Guide

### Running the Task Manager
```bash
# Launch interactive TUI and embedded Web GUI (port 8080)
./mem_monitor

# Launch with custom HTTP port
./mem_monitor --port 9090

# Launch in headless JSON streaming mode (for CI or piping)
./mem_monitor --headless --json
```

### Interacting via TUI
- `Up` / `Down` Arrow keys: Navigate through process table
- `p`: Sort processes by CPU % descending
- `m`: Sort processes by Memory (RSS) descending
- `k`: Send `SIGKILL` or custom signal to highlighted process
- `s`: Send `SIGSTOP` (pause process execution)
- `c`: Send `SIGCONT` (resume paused process)
- `q`: Exit cleanly and restore terminal modes

### Using the Custom Allocator on External Binaries
```bash
# Trace memory operations of any Linux binary
LD_PRELOAD=./libmyalloc.so ls -la

# Run a Python script using the custom allocator
LD_PRELOAD=./libmyalloc.so python3 -c "print([x**2 for x in range(100000)])"
```

---

## 6. Directory Layout
```
.
├── .editorconfig
├── .gitignore
├── .lefthook.yml
├── LICENSE
├── Makefile
├── README.md
├── SECURITY.md
├── CONTRIBUTING.md
├── .github/workflows/ci.yml
├── docs/
│   ├── architecture/
│   │   ├── adr/
│   │   │   ├── ADR-001-custom-allocator-design.md
│   │   │   ├── ADR-002-proc-parsing-strategy.md
│   │   │   ├── ADR-003-cli-tui-engine.md
│   │   │   └── ADR-004-realtime-gui-telemetry.md
│   │   ├── context.md
│   │   ├── container.md
│   │   └── data-flow.md
│   └── runbooks/
│       ├── incident-response.md
│       └── debugging-memory-leaks.md
├── include/
│   ├── allocator.h
│   ├── common.h
│   ├── gui_server.h
│   ├── proc_parser.h
│   └── tui.h
├── src/
│   ├── allocator/
│   │   ├── allocator.c
│   │   ├── free_list.c
│   │   └── preload_shim.c
│   ├── monitor/
│   │   ├── main.c
│   │   ├── proc_parser.c
│   │   ├── signal_handler.c
│   │   └── gui_server.c
│   └── ui/
│       └── tui.c
├── web/
│   ├── index.html
│   ├── style.css
│   └── app.js
├── tests/
│   ├── unity/
│   │   ├── unity.c
│   │   └── unity.h
│   ├── test_allocator.c
│   ├── test_proc_parser.c
│   └── integration_test.sh
└── scripts/
    ├── benchmark.sh
    └── stress_test.sh
```

---

## 7. License
Distributed under the MIT License. See [LICENSE](LICENSE) for details.
