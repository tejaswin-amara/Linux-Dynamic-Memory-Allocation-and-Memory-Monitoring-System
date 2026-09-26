# Security Policy & Threat Model

The **Linux Dynamic Memory Allocation & System Task Manager** suite executes at the boundary of user-space applications and the Linux kernel. This document outlines our supported versions, threat modeling, vulnerability disclosure procedures, and built-in security defenses.

---

## 1. Supported Versions

Security fixes and hardening patches are actively maintained for the following versions:

| Version | Supported | Maintenance Status |
|:---:|:---:|---|
| **1.0.x** | :white_check_mark: | Active production / Capstone baseline |
| **< 1.0.0** | :x: | Unsupported legacy development tags |

---

## 2. Threat Model & Security Architecture

### 2.1 Custom Dynamic Memory Allocator (`libmyalloc.so`)

| Threat Vector | Risk Description | Architectural Countermeasure |
|---|---|---|
| **Buffer Overrun / Out-of-Bounds Write** | An application writes past its allocated buffer boundary, corrupting adjacent heap chunks. | **Canary Boundary Tags**: Every block is enclosed by a 32-bit header canary (`0xDEADBEEF`) and a 32-bit footer canary (`0xBEEFDEAD`). Any overwrite is caught immediately during `my_free()` or `my_realloc()`, triggering a write-safe fatal abort message (`[FATAL] Heap corruption: ...`) and terminating via `fatal_abort()` before corruption propagates. |
| **Double-Free / Use-After-Free Attack** | An application attempts to free an already-freed pointer or realloc a freed pointer. | **State Flag & Abort**: Block headers track `is_free = 1`. If `my_free()` or `my_realloc()` is invoked on a free block, execution immediately halts via `fatal_abort()` with `[FATAL] Double free or use after free detected...`. |
| **Heap Metasploit / Pointer Tampering** | Manipulating `next`/`prev` pointers in segregated free lists to achieve arbitrary write primitives. | Segregated bins enforce size boundary consistency checks and header magic canary verification prior to list unlinking in `free_list_remove()`. Internal free-list manipulation symbols are hidden with `-fvisibility=hidden`. |
| **SUID / SGID Binary Hijacking** | Forcing privileged root binaries to load `libmyalloc.so` via `LD_PRELOAD`. | The Linux dynamic linker (`ld.so`) automatically ignores `LD_PRELOAD` for binaries with the `setuid` or `setgid` bit set unless invoked by the superuser (`CAP_SETUID`). |
| **Denial of Service (OOM Exhaustion)** | Unbounded heap growth triggering the Linux Out-Of-Memory (OOM) killer. | High-volume allocations (≥ 128 KB) bypass the heap segment and use anonymous `mmap()`, allowing immediate return of memory pages to the OS via `munmap()` upon release. |

### 2.2 Process & VFS Telemetry Engine (`mem_monitor`)

| Threat Vector | Risk Description | Architectural Countermeasure |
|---|---|---|
| **File Descriptor Exhaustion** | Rapidly opening `/proc` files across thousands of PIDs exhausts system file limits. | Every unbuffered `open(2)` in `proc_parser.c` is guaranteed a matching `close(2)` on all success and error code paths. |
| **Uncontrolled Process Termination** | Unauthorized callers dispatching `SIGKILL` or `SIGTERM` to critical OS processes (e.g., PID 1 / `systemd`). | `signal_handler.c` enforces boundary checks (`pid > 1`) and relies strictly on kernel-level POSIX credential validation (`kill(2)` errors with `EPERM` if calling process lacks capability). `/api/process/signal` requires a valid Bearer / X-Auth-Token. |
| **Race Conditions in Telemetry Exchange** | HTTP worker thread reading telemetry snapshots while the monitoring thread updates process records. | Synchronization is enforced via a POSIX read-write lock (`pthread_rwlock_t`). Telemetry writes take exclusive writer locks; HTTP clients acquire shared reader locks. |
| **Embedded HTTP Daemon Exploitation & CORS** | Cross-Origin Resource Sharing or unauthorized signal dispatch. | The HTTP daemon enforces constant-time Bearer / X-Auth-Token authentication, supports CORS header defaults on telemetry routes, and validates request paths cleanly without shell expansion or inline script rendering. |

---

## 3. Reporting a Vulnerability

If you identify a security vulnerability, buffer defect, or potential privilege escalation in this codebase:

1. **Do NOT open a public issue or discussion** on GitHub.
2. Send a detailed report to the security maintainers:
   - **Primary Contact**: `tejaswin.amara@gmail.com`
3. Please include in your report:
   - Detailed description of the vulnerability and attack vector.
   - Proof of Concept (PoC) code or step-by-step reproduction instructions.
   - Component affected (`libmyalloc.so`, `proc_parser`, `gui_server`, or `tui`).
   - Potential impact assessment.

### Response Timelines
- **Initial Acknowledgment**: Within 48 hours of receipt.
- **Triage & Reproduction**: Within 5 business days.
- **Patch Development & Validation**: Within 14 business days.
- **Public Advisory / Coordinated Disclosure**: Released concurrently with the patch.

---

## 4. Security Hardening in CI Pipeline

Our GitHub Actions pipeline continuously verifies security and memory cleanliness:
- **Clang-Format Enforcement**: Strict stylistic conformance preventing obfuscation.
- **AddressSanitizer (ASan) & UBSan**: Traps out-of-bounds accesses, integer overflows, and alignment faults.
- **Valgrind Memcheck**: Enforces zero memory leaks and zero uninitialized variable reads.
