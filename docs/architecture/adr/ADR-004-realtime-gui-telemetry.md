# ADR-004: Embedded C HTTP Telemetry Engine for Web GUI

## Status
Accepted

## Context
To satisfy the dual-interface requirement (CLI + Web/GUI), telemetry collected from the Linux kernel VFS must be streamed to an embedded modern web dashboard without requiring external runtimes (e.g. Node.js, Python, or Go).

## Decision
1. **Embedded C HTTP Server (`gui_server.c`)**:
   - Implement a lightweight, single-process POSIX socket server listening on a configurable TCP port (default `8080`).
   - Runs in a detached background POSIX thread (`pthread_create`), allowing the CLI/TUI and background server to share in-memory telemetry structures safely protected by a read-write lock (`pthread_rwlock_t`).
2. **HTTP API Contract**:
   - `GET /api/metrics`: Serializes the current snapshot of global CPU, memory, and top processes into clean JSON.
   - `POST /api/process/signal`: Receives JSON `{ "pid": 1234, "signal": "SIGTERM" }` and safely calls `kill(pid, sig)`.
   - `GET /`, `GET /style.css`, `GET /app.js`: Serves embedded static web assets from the local filesystem or memory buffers.
3. **Frontend Architecture**:
   - Pure vanilla HTML5, CSS3, and modern ES6 JavaScript.
   - Canvas-based live telemetry charts for CPU and RAM history.
   - Filterable, sortable process table with action buttons to send signals.

## Consequences
- Zero external runtime dependencies; the complete task manager compiles into a standalone C executable.
- Decoupled architecture: web interface can be enabled or disabled via CLI flag (`--no-web` / `--port`).
