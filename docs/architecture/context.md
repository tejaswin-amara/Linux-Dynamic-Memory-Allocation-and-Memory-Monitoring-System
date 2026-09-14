# System Context Architecture

The Linux Dynamic Memory Allocation & System Task Manager operates as a unified systems utility on Linux platforms (Ubuntu 24.04 LTS), interacting with kernel interfaces, user applications, and monitoring clients.

## Mermaid System Context Diagram

```mermaid
C4Context
    title System Context Diagram - Linux Task Manager & Custom Allocator

    Person(developer, "Systems Engineer / Operator", "Monitors system resources, inspects memory allocations, and issues POSIX signals")
    
    System(task_manager, "mem_monitor", "High-performance process and virtual memory monitoring daemon with TUI and embedded Web GUI")
    System(custom_allocator, "libmyalloc.so", "Custom dynamic memory allocator using segregated free lists, sbrk, and mmap")
    
    System_Ext(target_apps, "Target User Applications", "Arbitrary Linux binaries linked or preloaded with libmyalloc.so")
    System_Ext(kernel_vfs, "Linux Kernel VFS (/proc)", "Exposes stat, meminfo, and per-process maps/status")
    System_Ext(browser, "Web Browser Dashboard", "HTML5/JS client rendering live charts and receiving JSON telemetry")

    Rel(developer, task_manager, "Interacts via ncurses TUI", "Keyboard binds")
    Rel(developer, browser, "Views graphical dashboard", "HTTPS / HTTP")
    Rel(browser, task_manager, "Fetches telemetry & sends signals", "HTTP REST /api/metrics")
    Rel(task_manager, kernel_vfs, "Reads unbuffered VFS statistics", "open(), read(), close()")
    Rel(task_manager, target_apps, "Controls process lifecycle", "kill(pid, sig)")
    Rel(target_apps, custom_allocator, "Delegates dynamic allocations", "malloc, free, realloc, calloc")
    Rel(custom_allocator, kernel_vfs, "Expands heap or creates anonymous mappings", "sbrk(), mmap(), munmap()")
```
