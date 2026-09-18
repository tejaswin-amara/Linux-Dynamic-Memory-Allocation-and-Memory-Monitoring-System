# ADR-004: Embedded C HTTP Telemetry Engine for Web GUI

## Status
**Accepted** (Implemented in `src/monitor/gui_server.c`)

## Context & Problem Statement
To satisfy the dual-interface requirement (CLI + Web GUI) for the **KLEF 25CS2104E Capstone** (Course Outcomes CO3 and CO6), telemetry aggregated from the Linux kernel VFS must be delivered in real-time to a modern graphical browser interface.

Typical web-based system monitors introduce external runtimes (such as Python Flask/FastAPI, Node.js Express, or Go daemons) to bridge OS telemetry to the browser. In a low-level systems programming context, this approach introduces significant drawbacks:
1. **Bloated Footprint**: Node.js or Python runtimes consume 30 MB to 150 MB of memory and multiple processes, dwarf the footprint of the monitoring engine itself.
2. **Complex Toolchains**: Introduces non-C dependencies (`npm`, `pip`, Python virtual environments) into what should be a self-contained C project.
3. **IPC Overhead**: Requires serializing and transmitting metrics across external IPC channels (pipes, domain sockets) before reaching the web server.

We required a zero-dependency, self-contained HTTP telemetry server compiled directly into the `mem_monitor` executable.

## Decision Drivers
- **Zero External Runtimes**: The complete system must compile via standard `make` without requiring Node.js, Python, or external package managers.
- **Minimal Resource Overhead**: Embedded daemon must consume $< 1\text{ MB}$ of memory.
- **Thread Safety & Non-Interference**: HTTP clients connecting or disconnecting must never block or delay the main terminal sampling loop.
- **RESTful JSON Contract**: Standard HTTP/1.1 JSON streaming allowing easy consumption by web browsers or automated CLI tools (`curl`/`jq`).

## Considered Alternatives
1. **External Web Daemon (Node.js / Python Flask)**:
   - *Pros*: Rapid development of web server logic.
   - *Cons*: Heavy runtime dependencies; violates self-contained C systems programming philosophy.
2. **Third-Party C Web Libraries (`libmicrohttpd` / `mongoose` / `civetweb`)**:
   - *Pros*: Battle-tested HTTP parsing.
   - *Cons*: Introduces external library dependencies, build system complexities, and potential licensing friction.
3. **Embedded POSIX Socket HTTP Server (`gui_server.c`) (Selected)**:
   - *Pros*: Zero external dependencies; built purely with standard POSIX socket API (`sys/socket.h`, `netinet/in.h`) and `pthreads`; direct in-memory access to telemetry snapshots.

## Decision Outcome
We implemented a **lightweight, multi-threaded embedded C HTTP daemon**:

### 1. POSIX Socket Architecture
- Initializes an `AF_INET`, `SOCK_STREAM` TCP socket with `SO_REUSEADDR`.
- Binds to port `8080` (customizable via `--port <number>`).
- Operates on a dedicated worker thread spawned via `pthread_create(&server->thread, NULL, gui_server_worker, server)`.

### 2. Synchronization Model (`pthread_rwlock_t`)
The monitoring loop and HTTP worker communicate via a shared `system_snapshot_t` structure protected by a POSIX Read-Write Lock:
- **Writer (Monitor Loop)**: Acquires an exclusive write lock (`pthread_rwlock_wrlock`) during the microsecond memory copy of fresh telemetry.
- **Readers (HTTP Workers)**: Acquire shared read locks (`pthread_rwlock_rdlock`) during snapshot serialization into JSON, allowing multiple concurrent web clients to read without contention.

### 3. REST API Contract & Static File Serving
- **Static Assets**:
  - `GET /` or `GET /index.html` $\rightarrow$ Serves `web/index.html` (`text/html`).
  - `GET /style.css` $\rightarrow$ Serves `web/style.css` (`text/css`).
  - `GET /app.js` $\rightarrow$ Serves `web/app.js` (`application/javascript`).
- **Telemetry Stream**:
  - `GET /api/metrics` $\rightarrow$ Serializes the current snapshot into an HTTP/1.1 200 OK JSON response containing CPU%, per-core count, memory/swap capacity and utilization, and top process records.
- **Signal Control**:
  - `POST /api/process/signal` $\rightarrow$ Parses request body `{"pid": <PID>, "signal": "<NAME>"}`, validates PID bounds (`pid > 1`), translates the signal name via `signal_parse_name()`, and calls `kill(pid, sig)`.

### 4. Frontend Architecture
The web client is built with standard vanilla web technologies:
- Responsive semantic HTML5 markup.
- Modern CSS3 with dark-mode styling and fluid progress meters.
- ES6 JavaScript using the `Fetch API` polling every 1000 ms, updating DOM nodes without page reloads.

## Consequences

### Positive
- **Single Standalone Executable**: The entire task manager (engine, TUI, and Web GUI) builds into one compact binary (`mem_monitor`).
- **Negligible Footprint**: Memory usage remains under 2 MB for the entire running system.
- **API Extensibility**: Telemetry can be queried by external observability tools or scripts (`curl -s http://localhost:8080/api/metrics | jq .`).

### Negative
- **HTTP/1.1 Sub-protocol**: Only implements minimal subset of HTTP/1.1 required for static files and REST JSON; lacks chunked transfer encoding, keep-alive pipelining, and TLS encryption (production deployments require a reverse proxy such as Nginx or Caddy for HTTPS).

## Verification & Compliance
- **API Functional Tests**: Integration verified in `tests/integration_test.sh` via curl and headless snapshot validation.
- **Browser Testing**: Validated on Google Chrome, Mozilla Firefox, and Safari on desktop and mobile viewports.
- **Valgrind**: Verified zero memory leaks during HTTP requests and JSON buffer allocation.
