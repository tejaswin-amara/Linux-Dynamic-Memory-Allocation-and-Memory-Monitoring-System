# ADR-004: Embedded C HTTP Telemetry Engine for Web GUI

## Status
**Accepted** (Implemented in `src/monitor/gui_server.c`)

## Context & Problem Statement
To satisfy the dual-interface requirement (CLI + Web GUI) for the **KLEF 25CS2104E Capstone** (Course Outcomes CO3 and CO6), telemetry aggregated from the Linux kernel VFS must be delivered in real-time to a modern graphical browser interface.

Typical web-based system monitors introduce external runtimes (such as Python Flask/FastAPI, Node.js Express, or Go daemons) to bridge OS telemetry to the browser. In a low-level systems programming context, this approach introduces significant drawbacks:
1. **Bloated Footprint**: Node.js or Python runtimes consume 30 MB to 150 MB of memory and multiple processes, dwarfing the footprint of the monitoring engine itself.
2. **Complex Toolchains**: Introduces non-C dependencies (`npm`, `pip`, Python virtual environments) into what should be a self-contained C project.
3. **IPC Overhead**: Requires serializing and transmitting metrics across external IPC channels (pipes, domain sockets) before reaching the web server.

We required a zero-dependency, self-contained HTTP telemetry server compiled directly into the `mem_monitor` executable.

## Decision Drivers
- **Zero External Runtimes**: The complete system must compile via standard `make` without requiring Node.js, Python, or external package managers.
- **Measured Resource Overhead**: In the Ubuntu 24.04 CI runner, a GCC build measured after approximately 3 seconds and one `/api/metrics` request at **VmRSS 4056 kB** total resident memory, comprising **RssAnon 1796 kB** of anonymous resident memory and **RssFile 2260 kB** of file-backed resident memory. `VmRSS` is the total resident set; `RssAnon` is the anonymous portion. The prior "< 1 MB" decision driver was unmeasured and is removed.
- **Thread Safety & Non-Interference**: HTTP clients connecting or disconnecting must never block or delay the main terminal sampling loop.
- **Hardened Local Binding & Authentication**: Restrict external network exposure by default and require constant-time token verification on privileged signal endpoints.
- **RESTful JSON Contract**: Standard HTTP/1.1 JSON responses allowing easy consumption by web browsers or automated CLI tools (`curl`/`jq`).

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

### 1. POSIX Socket & Binding Architecture
- **Local Isolation by Default**: Binds to `127.0.0.1` by default to prevent unauthorized network access. The bind host can be explicitly overridden via `--bind` / `--host`.
- **Customizable Port**: Defaults to port `8080` (customizable via `--port <number>`).
- **Main Accept Loop**: Initializes an `AF_INET`, `SOCK_STREAM` TCP socket with `SO_REUSEADDR` and runs a listener thread (`gui_server_worker`). Socket timeout (`SO_RCVTIMEO` set to 1s) ensures periodic checks of the `server->is_running` loop flag during shutdown.

### 2. Per-Connection Detached-Thread Model
- **Worker Concurrency Cap**: For each incoming TCP connection, `gui_server_worker` checks active workers against **`MAX_CLIENT_WORKERS (32)`**. If the cap is reached, it returns an HTTP `503 Service Unavailable` response (`{"error": "Too many concurrent connections"}`) and closes the client socket immediately.
- **Detached Client Workers**: When within limits, worker count is incremented under `worker_mutex`, 5-second socket receive/send timeouts (`SO_RCVTIMEO` / `SO_SNDTIMEO`) are applied to the accepted client socket, and a detached worker thread (`client_thread_worker`) is spawned via `pthread_create` with `PTHREAD_CREATE_DETACHED`.
- **Clean Resource Teardown**: Upon completion, `client_thread_worker` closes the socket, decrements `active_workers`, and signals `worker_cond`. During server shutdown (`gui_server_stop`), the main server waits on `worker_cond` until all active client workers have exited.

### 3. Signal Resilience (`SIGPIPE` Handling)
- Signal `SIGPIPE` is ignored system-wide on startup (`signal(SIGPIPE, SIG_IGN)`).
- All outbound socket transmissions (`send_all`) use the `MSG_NOSIGNAL` flag. This prevents process termination when writing to client connections closed prematurely by browser refresh or disconnect.

### 4. Token Authentication on Signal Endpoint
- **Token Configuration**: Configured via `--token <value>`. If omitted or empty, an auto-generated 64-character hexadecimal token is generated from `/dev/urandom` and logged at startup (`Generated auth token: <token>`).
- **Header Inspection**: `POST /api/process/signal` requires a valid token provided via `X-Auth-Token: <token>` or `Authorization: Bearer <token>`.
- **Constant-Time Verification**: Token validation uses constant-time string comparison (`constant_time_streq`) to eliminate timing side-channel leaks. Requests lacking a valid token receive HTTP `401 Unauthorized` (`{"error": "Unauthorized"}`).

### 5. Synchronization Model (`pthread_rwlock_t`)
The monitoring loop and HTTP workers communicate via a shared `system_snapshot_t` structure protected by a POSIX Read-Write Lock:
- **Writer (Monitor Loop)**: Acquires an exclusive write lock (`pthread_rwlock_wrlock`) during the microsecond memory copy of fresh telemetry via `gui_server_update_snapshot`.
- **Readers (HTTP Workers)**: Acquire shared read locks (`pthread_rwlock_rdlock`) in `serve_metrics_json` during JSON serialization into dynamically allocated heap memory. Multiple readers can serialize concurrently, but the read lock remains held for the serialization duration and can therefore delay the monitor's exclusive snapshot update.

### 6. REST API Contract, CORS & Static Assets
- **Static Assets**:
  - `GET /` or `GET /index.html` $ightarrow$ Serves `web/index.html` (`text/html`).
  - `GET /style.css` $ightarrow$ Serves `web/style.css` (`text/css`).
  - `GET /app.js` $ightarrow$ Serves `web/app.js` (`application/javascript`).
- **Telemetry Stream & CORS Policy**:
  - `GET /api/metrics` $ightarrow$ Serializes snapshot into an HTTP/1.1 200 OK JSON response containing CPU%, per-core count, memory capacity and utilization, and process array.
  - **CORS Scope**: `Access-Control-Allow-Origin: *` is attached **exclusively to `/api/metrics`** to enable browser cross-origin telemetry polling. It is omitted from `/api/process/signal`; token validation remains the authorization control for signal requests.
- **Signal Control**:
  - `POST /api/process/signal` $ightarrow$ Authenticates token, parses JSON payload `{"pid": <PID>, "signal": "<NAME>"}`, enforces PID bounds ($1 < 	ext{PID} le 4194304$), resolves signal name via `signal_parse_name()`, and dispatches signal via `signal_send_to_process()`.

### 7. Frontend Architecture
The web client is built with standard vanilla web technologies:
- Responsive semantic HTML5 markup.
- Modern CSS3 with dark-mode styling and fluid progress meters.
- ES6 JavaScript using the `Fetch API` polling every 1000 ms, updating DOM nodes without page reloads.

## Consequences

### Positive
- **Single Standalone Executable**: The entire task manager (engine, TUI, and Web GUI) builds into one compact binary (`mem_monitor`).
- **Measured Resident Footprint**: The same GCC CI measurement recorded **VmRSS 4056 kB** (~3.96 MiB) total resident memory, with **RssAnon 1796 kB** (~1.75 MiB) anonymous resident memory and **RssFile 2260 kB** (~2.21 MiB) file-backed resident memory. These are measured resident-set figures, not a sustained upper bound or a worst-case concurrent-request ceiling.
- **Robust Security Perimeter**: Local binding by default, constant-time token authentication on process signaling, and CORS headers restricted strictly to read-only metrics.
- **High Concurrency & Stability**: Per-connection detached threads bounded by `MAX_CLIENT_WORKERS` with socket timeouts and `SIGPIPE` immunity.

### Negative
- **HTTP/1.1 Sub-protocol**: Only implements minimal subset of HTTP/1.1 required for static files and REST JSON; lacks chunked transfer encoding, keep-alive pipelining, and TLS encryption (production deployments exposed over untrusted networks require a reverse proxy such as Nginx or Caddy for HTTPS).

## Verification & Compliance
- **API Functional Tests**: Integration verified in `tests/integration_test.sh` via curl and headless snapshot validation.
- **Valgrind Soak Testing**: Verified in `scripts/valgrind_soak.sh` as mem_monitor under Valgrind (Memcheck's allocator replaces libmyalloc.so in this run; see `scripts/selfhosted_soak.sh` for the allocator soak).
