# Linux Dynamic Memory Allocation & System Task Manager

[![Systems CI Pipeline](https://github.com/tejaswin-amara/Linux-Dynamic-Memory-Allocation-and-Memory-Monitoring-System/actions/workflows/ci.yml/badge.svg)](https://github.com/tejaswin-amara/Linux-Dynamic-Memory-Allocation-and-Memory-Monitoring-System/actions/workflows/ci.yml)
[![C11](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform](https://img.shields.io/badge/platform-GNU%2FLinux-orange.svg)](https://www.kernel.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A systems-programming capstone for **KLEF 25CS2104E: Outside-In Operating Systems & Systems Programming**.

The project combines two low-level Linux components:

- **\`libmyalloc.so\`** — a custom dynamic memory allocator implemented in C, using segregated free lists, boundary metadata, \`sbrk()\`, \`mmap()\`, and \`LD_PRELOAD\` interception.
- **\`mem_monitor\`** — a Linux process and memory monitor that reads the **\`/proc\` virtual filesystem** directly and exposes the collected telemetry through both an **\`ncurses\` TUI** and an embedded **HTTP/JSON web dashboard**.

The repository is intentionally focused on operating-system concepts, POSIX/Linux system calls, virtual memory, process inspection, signals, synchronization, and systems testing.

---

## Features

### Custom memory allocator

- 16-byte block alignment.
- **10 segregated free-list size classes** with best-fit search within each class.
- Boundary metadata with:
  - Header magic: \`0xDEADBEEF\`
  - Footer magic: \`0xBEEFDEAD\`
- Block splitting and forward/backward coalescing for reusable heap blocks.
- Hybrid allocation strategy:
  - Aligned block size **below 128 KiB** → heap growth/reuse through \`sbrk()\`
  - Aligned block size **at or above 128 KiB** → anonymous \`mmap()\`
- \`malloc()\`, \`free()\`, \`calloc()\`, and \`realloc()\` interception through \`LD_PRELOAD\`.
- Allocator statistics for requested allocations, frees, \`sbrk()\` allocations, and \`mmap()\` allocations.
- Mutex protection around allocator state for concurrent access.

### Linux process and memory monitor

- Direct reads from Linux \`/proc\` using low-level \`open()\`, \`read()\`, and \`close()\`.
- System CPU metrics from \`/proc/stat\`.
- RAM and swap metrics from \`/proc/meminfo\`.
- Per-process information from \`/proc/<pid>/stat\` and \`/proc/<pid>/status\`.
- Tracks:
  - PID / PPID
  - Command name
  - Process state
  - Thread count
  - CPU usage
  - Virtual memory size
  - Resident set size (RSS)
  - Voluntary and non-voluntary context switches
- Differential CPU accounting using successive jiffy snapshots.
- Online CPU-core detection via \`sysconf(_SC_NPROCESSORS_ONLN)\`.
- Process discovery capped at **2048 processes per snapshot**.

### Terminal UI

The TUI is built with **\`ncurses\`** and refreshes the process view every 250 ms.

Controls:

| Key | Action |
|---|---|
| \`↑\` / \`↓\` | Move through processes |
| \`j\` / \`k\` | Move down / up |
| \`p\` | Sort by CPU usage |
| \`m\` | Sort by RSS memory |
| \`s\` | Send \`SIGSTOP\` |
| \`c\` | Send \`SIGCONT\` |
| \`K\` | Send \`SIGKILL\` |
| \`q\` | Quit |

### Web dashboard

The embedded server serves the static dashboard and exposes JSON telemetry.

Supported routes:

| Method | Route | Purpose |
|---|---|---|
| \`GET\` | \`/\` | Web dashboard |
| \`GET\` | \`/index.html\` | Dashboard HTML |
| \`GET\` | \`/style.css\` | Dashboard CSS |
| \`GET\` | \`/app.js\` | Dashboard JavaScript |
| \`GET\` | \`/api/metrics\` | Current CPU, memory, swap, and process telemetry |
| \`POST\` | \`/api/process/signal\` | Dispatch a POSIX signal to a target PID |

Example signal request:

\`\`\`json
{
  "pid": 1234,
  "signal": "SIGTERM"
}
\`\`\`

Supported signal names/numeric aliases include:

- \`SIGKILL\` / \`9\`
- \`SIGTERM\` / \`15\`
- \`SIGSTOP\` / \`19\`
- \`SIGCONT\` / \`18\`
- \`SIGINT\` / \`2\`

---

## Architecture

\`\`\`mermaid
graph TD
    APP["Target Linux Application"] -->|"LD_PRELOAD"| ALLOC["libmyalloc.so"]
    ALLOC --> FL["10 Segregated Free Lists"]
    ALLOC -->|"Block size < 128 KiB"| SBRK["sbrk()"]
    ALLOC -->|"Block size >= 128 KiB"| MMAP["mmap()"]

    SBRK --> HEAP["Process Heap"]
    MMAP --> MAPS["Anonymous Mapping"]

    MON["mem_monitor"] --> PROC["/proc filesystem"]
    PROC --> CPU["/proc/stat"]
    PROC --> MEM["/proc/meminfo"]
    PROC --> PSTAT["/proc/<pid>/stat"]
    PROC --> STATUS["/proc/<pid>/status"]

    MON --> SNAP["In-memory system snapshot"]
    SNAP --> TUI["ncurses TUI"]
    SNAP --> HTTP["Embedded HTTP server"]
    HTTP --> WEB["Web dashboard"]
    WEB -->|"GET /api/metrics"| HTTP
    WEB -->|"POST /api/process/signal"| SIG["kill(pid, sig)"]
    SIG --> APP
\`\`\`

### Runtime model

1. \`mem_monitor\` initializes the \`/proc\` parser and detects the number of online CPU cores.
2. The main loop captures a fresh system snapshot.
3. The snapshot is copied into the HTTP server state under a \`pthread_rwlock_t\`.
4. In normal mode, the snapshot is rendered in the TUI.
5. The embedded HTTP server runs in a background pthread and serves the web dashboard/API.
6. In headless mode, the TUI is skipped while the snapshot continues to refresh once per second.
7. Signal requests are translated into POSIX signal numbers and dispatched with \`kill()\`.

---

## CPU and memory calculations

### System CPU

System CPU utilization is calculated from the change in total and idle jiffies between consecutive readings:

\`\`\`
busy = delta_total - delta_idle
CPU% = (busy / delta_total) × 100
\`\`\`

### Per-process CPU

Per-process CPU usage is derived from the process \`utime + stime\` delta relative to the total system jiffy delta and scaled by the number of online cores:

\`\`\`
Process CPU% =
    (process_delta / system_delta) × 100 × online_core_count
\`\`\`

### Memory

- RAM usage uses **MemTotal - MemAvailable** from \`/proc/meminfo\`.
- Swap usage uses **SwapTotal - SwapFree**.
- Per-process memory percentage is based on **VmRSS / MemTotal**.

---

## Curriculum traceability — 25CS2104E

| Course outcome | Relevant implementation |
|---|---|
| **CO1 — OS service layer** | \`sbrk()\`, \`mmap()\`, \`munmap()\`, \`open()\`, \`read()\`, \`close()\`, \`kill()\`, \`sysconf()\` |
| **CO2 — Process control** | \`/proc/<pid>/stat\` parsing, process states, PID/PPID tracking, thread counts, process CPU deltas |
| **CO3 — Inter-process communication** | POSIX signal dispatch with \`kill()\` and HTTP/TCP delivery of JSON telemetry |
| **CO4 — Memory management** | Custom allocator, segregated free lists, block splitting/coalescing, boundary metadata, \`sbrk()\` / \`mmap()\` strategy |
| **CO5 — File systems / VFS** | Direct parsing of Linux \`/proc\` using low-level file-descriptor I/O |
| **CO6 — Concurrency** | Allocator \`pthread_mutex_t\` and HTTP snapshot protection with \`pthread_rwlock_t\` |

---

## Requirements

Tested repository target:

- Ubuntu **24.04 LTS** or a compatible GNU/Linux environment
- GCC or Clang
- GNU Make
- \`libncurses-dev\`
- \`valgrind\`
- \`clang-format\` for formatting checks

Install the required packages on Ubuntu:

\`\`\`bash
sudo apt-get update
sudo apt-get install -y build-essential gcc clang make valgrind libncurses-dev clang-format
\`\`\`

---

## Build

Build the allocator and monitor:

\`\`\`bash
make all
\`\`\`

This produces:

- \`libmyalloc.so\`
- \`mem_monitor\`

Remove generated artifacts:

\`\`\`bash
make clean
\`\`\`

See all Make targets:

\`\`\`bash
make help
\`\`\`

---

## Running the monitor

### Normal mode — TUI + web server

\`\`\`bash
./mem_monitor
\`\`\`

Open the dashboard at:

\`http://localhost:8080\`

### Custom HTTP port

\`\`\`bash
./mem_monitor --port 9090
\`\`\`

Dashboard:

\`http://localhost:9090\`

### Headless server mode

Runs the HTTP server without starting ncurses:

\`\`\`bash
./mem_monitor --headless
\`\`\`

### One-shot JSON snapshot

\`--json\` implies headless mode, prints one JSON snapshot, and exits:

\`\`\`bash
./mem_monitor --json
\`\`\`

This is suitable for CI or shell pipelines.

---

## Using the allocator with LD_PRELOAD

Run a dynamically linked Linux program through the custom allocator:

\`\`\`bash
LD_PRELOAD=./libmyalloc.so /bin/ls -la /tmp
\`\`\`

Example with Python:

\`\`\`bash
LD_PRELOAD=./libmyalloc.so python3 -c "print([x**2 for x in range(100000)])"
\`\`\`

The allocator is an educational systems-programming implementation rather than a hardened replacement for glibc's allocator. Use it in controlled Linux environments.

---

## Testing and verification

### Unit + integration tests

\`\`\`bash
make test
\`\`\`

This builds and runs:

- Allocator unit tests
- \`/proc\` parser unit tests
- Signal-handler unit tests
- LD_PRELOAD integration checks
- One-shot JSON telemetry integration checks

### AddressSanitizer + UndefinedBehaviorSanitizer

\`\`\`bash
make asan
\`\`\`

The target recompiles the project and test binaries with ASan/UBSan enabled.

### Valgrind

\`\`\`bash
make valgrind
\`\`\`

Runs leak checks for the allocator, parser, and signal-handler tests.

### Allocator benchmark

\`\`\`bash
make benchmark
\`\`\`

Runs allocation/free workloads with a glibc baseline and the custom allocator via \`LD_PRELOAD\`.

### Concurrency and fragmentation stress tests

\`\`\`bash
bash scripts/stress_test.sh
\`\`\`

The stress suite exercises multi-threaded allocation patterns and variable-size workloads.

---

## Repository structure

\`\`\`text
.
├── .github/
│   └── workflows/
│       └── ci.yml
├── docs/
│   ├── architecture/
│   │   ├── adr/
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
│   │   ├── gui_server.c
│   │   ├── main.c
│   │   ├── proc_parser.c
│   │   └── signal_handler.c
│   └── ui/
│       └── tui.c
├── tests/
│   ├── unity/
│   │   ├── unity.c
│   │   └── unity.h
│   ├── test_allocator.c
│   ├── test_proc_parser.c
│   ├── test_signal_handler.c
│   └── integration_test.sh
├── scripts/
│   ├── benchmark.sh
│   └── stress_test.sh
├── web/
│   ├── index.html
│   ├── style.css
│   └── app.js
├── .editorconfig
├── .gitignore
├── .lefthook.yml
├── CONTRIBUTING.md
├── LICENSE
├── Makefile
├── README.md
└── SECURITY.md
\`\`\`

---

## CI

GitHub Actions runs the project on **Ubuntu 24.04** with both:

- GCC
- Clang

The workflow checks:

1. Formatting with \`clang-format\`
2. Strict compilation
3. Unit and integration tests
4. ASan/UBSan builds
5. Valgrind leak checks
6. Benchmark execution

---

## Security notes

The embedded web server is intentionally minimal and should be treated as a local/controlled development tool.

Important current behavior:

- The HTTP server binds to **\`INADDR_ANY\`**, not only loopback.
- The signal-control endpoint has **no authentication or authorization layer**.
- The metrics endpoint enables permissive **CORS (\`*\`)**.
- Do not expose the service to an untrusted network without adding access controls and network isolation.

See [SECURITY.md](SECURITY.md) for the repository's vulnerability-reporting policy.

---

## Current implementation notes

The README describes the implementation currently present in the repository. A few capabilities are intentionally limited:

- The process snapshot stores a \`pss_kb\` field, but the current parser does not populate PSS from \`smaps_rollup\`.
- The parser provides a helper for reading \`/proc/<pid>/maps\`, but that data is not part of the standard dashboard snapshot.
- Canary validation detects invalid allocator metadata, but the allocator does not maintain a dedicated double-free state table.
- The web API returns at most **100 processes** per metrics response, while the internal snapshot can hold up to 2048.
- \`allocator_verify_integrity()\` is currently a placeholder and should not be treated as a complete heap-wide integrity auditor.

These limitations are documented here rather than presenting unimplemented behavior as completed functionality.

---

## Documentation

- [Architecture context](docs/architecture/context.md)
- [Container architecture](docs/architecture/container.md)
- [Telemetry and allocation data flow](docs/architecture/data-flow.md)
- [Architecture Decision Records](docs/architecture/adr/)
- [Incident response runbook](docs/runbooks/incident-response.md)
- [Memory-leak debugging runbook](docs/runbooks/debugging-memory-leaks.md)
- [Contributing guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

---

## License

Distributed under the [MIT License](LICENSE).
