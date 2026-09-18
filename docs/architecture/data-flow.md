# Telemetry and Allocation Data-Flow Architecture

This document provides exhaustive sequence diagrams and event traces illustrating the runtime data paths across the **Linux Dynamic Memory Allocation & System Task Manager** system.

---

## 1. Metrics Aggregation & Web Streaming Flow

This sequence traces how system metrics flow from the kernel Virtual File System (`/proc`) through the zero-allocation parser into the snapshot store, and subsequently out to both the `ncurses` TUI and the Web GUI client:

```mermaid
sequenceDiagram
    autonumber
    actor Browser as Remote Web Browser
    actor Operator as Terminal Operator
    participant TUI as ncurses TUI (tui.c)
    participant Engine as Monitor Engine (main.c)
    participant Parser as VFS Parser (proc_parser.c)
    participant VFS as Linux Kernel (/proc)
    participant Store as Snapshot Store (gui_server_t)
    participant HTTP as HTTP Server (gui_server.c)

    loop Sampling Loop (Every 250ms - 1000ms)
        Engine->>Parser: proc_parser_take_snapshot(&snapshot)
        
        Parser->>VFS: open("/proc/stat", O_RDONLY)
        VFS-->>Parser: File descriptor
        Parser->>VFS: read(fd, stack_buf, 4096)
        VFS-->>Parser: CPU raw jiffies
        Parser->>VFS: close(fd)
        
        Parser->>VFS: open("/proc/meminfo", O_RDONLY)
        VFS-->>Parser: File descriptor
        Parser->>VFS: read(fd, stack_buf, 4096)
        VFS-->>Parser: RAM & Swap counters
        Parser->>VFS: close(fd)
        
        Parser->>VFS: opendir("/proc") & read /proc/[pid]/stat, status
        VFS-->>Parser: Process times, state, VM sizes
        
        Parser->>Parser: Calculate differential CPU% & memory percentages
        Parser-->>Engine: Completed system_snapshot_t

        Engine->>Store: gui_server_update_snapshot(&snapshot)
        Store->>Store: Acquire pthread_rwlock_wrlock()
        Store->>Store: Copy snapshot to latest_snapshot
        Store->>Store: Release pthread_rwlock_unlock()

        Engine->>TUI: tui_render(&snapshot, &state)
        TUI-->>Operator: Redraw terminal screen & ASCII meters
    end

    opt Client Telemetry Polling (Every 1000ms)
        Browser->>HTTP: GET /api/metrics HTTP/1.1
        HTTP->>Store: Acquire pthread_rwlock_rdlock()
        Store-->>HTTP: Read snapshot copy
        HTTP->>Store: Release pthread_rwlock_unlock()
        HTTP->>HTTP: Serialize snapshot to JSON payload
        HTTP-->>Browser: HTTP/1.1 200 OK (Content-Type: application/json)
        Browser->>Browser: Update live canvas graphs & process table
    end
```

---

## 2. Dynamic Memory Allocation Flow (`my_malloc`)

This sequence illustrates the hybrid allocation path inside `libmyalloc.so`, detailing bin searching, block splitting, program break expansion, and anonymous memory mapping:

```mermaid
sequenceDiagram
    autonumber
    actor App as Target Application
    participant Shim as preload_shim.c
    participant Alloc as allocator.c (my_malloc)
    participant Mutex as alloc_mutex
    participant Bins as Segregated Free Lists (10 Bins)
    participant Kernel as Linux Kernel VM

    App->>Shim: malloc(size)
    Shim->>Alloc: my_malloc(size)
    Alloc->>Alloc: total_size = ALIGN(sizeof(header) + size + sizeof(footer))

    Alloc->>Mutex: pthread_mutex_lock(&alloc_mutex)

    alt total_size >= 128 KB (Large Allocation Path)
        Alloc->>Kernel: mmap(NULL, total_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
        Kernel-->>Alloc: Mapped memory address
        Alloc->>Alloc: Format header (magic=0xDEADBEEF, is_mmap=1, size=total_size)
        Alloc->>Alloc: Format footer (magic=0xBEEFDEAD, size=total_size)
        Alloc->>Alloc: Increment global_stats.mmap_allocations
        Alloc->>Mutex: pthread_mutex_unlock(&alloc_mutex)
        Alloc-->>App: Return (header + 1) [16-byte aligned payload]

    else total_size < 128 KB (Small/Medium Segregated List Path)
        Alloc->>Bins: free_list_find_fit(total_size)
        
        alt Free block found in matching/higher bin
            Bins-->>Alloc: Return free block pointer
            alt Remainder >= min_split (32B + header + footer)
                Alloc->>Alloc: Split chunk into allocated block & remainder block
                Alloc->>Alloc: Format remainder header & footer (magic canaries)
                Alloc->>Bins: free_list_insert(remainder)
            end
        else No suitable free block in any bin
            Alloc->>Kernel: sbrk(total_size)
            Kernel-->>Alloc: Heap break advance address
            Alloc->>Alloc: Format header (magic=0xDEADBEEF, is_mmap=0, size=total_size)
            Alloc->>Alloc: Increment global_stats.sbrk_allocations
        end

        Alloc->>Alloc: Format footer (magic=0xBEEFDEAD, size=block_size)
        Alloc->>Alloc: Set header->is_free = 0
        Alloc->>Mutex: pthread_mutex_unlock(&alloc_mutex)
        Alloc-->>App: Return (header + 1) [16-byte aligned payload]
    end
```

---

## 3. Memory Deallocation & Bidirectional Coalescing (`my_free`)

This sequence traces boundary canary validation, adjacent block inspection, and bidirectional merging:

```mermaid
sequenceDiagram
    autonumber
    actor App as Target Application
    participant Alloc as allocator.c (my_free)
    participant Mutex as alloc_mutex
    participant Bins as Segregated Free Lists
    participant Kernel as Linux Kernel VM

    App->>Alloc: my_free(ptr)
    Alloc->>Alloc: header = (block_header_t *)ptr - 1
    Alloc->>Alloc: Verify header->magic_header == 0xDEADBEEF
    Alloc->>Alloc: Verify footer->magic_footer == 0xBEEFDEAD
    
    alt Canary Corruption Detected
        Alloc->>Alloc: LOG_ERROR("Heap corruption: Invalid canary")
        Alloc-->>App: Abort deallocation (fault isolation)
    end

    Alloc->>Mutex: pthread_mutex_lock(&alloc_mutex)

    alt header->is_mmap == 1 (Direct Unmap)
        Alloc->>Mutex: pthread_mutex_unlock(&alloc_mutex)
        Alloc->>Kernel: munmap(header, header->block_size)
        Kernel-->>Alloc: Success (0)
        Alloc-->>App: Return

    else header->is_mmap == 0 (Heap Chunks)
        Alloc->>Alloc: header->is_free = 1

        opt Forward Coalescing (Next Block)
            Alloc->>Alloc: next = (char *)header + header->block_size
            alt next < sbrk(0) AND next is free
                Alloc->>Bins: free_list_remove(next)
                Alloc->>Alloc: header->block_size += next->block_size
                Alloc->>Alloc: Update trailing footer
            end
        end

        opt Backward Coalescing (Previous Block)
            Alloc->>Alloc: prev_footer = (char *)header - sizeof(block_footer_t)
            alt prev_footer has valid canary AND prev block is free
                Alloc->>Alloc: prev = (char *)header - prev_footer->block_size
                Alloc->>Bins: free_list_remove(prev)
                Alloc->>Alloc: prev->block_size += header->block_size
                Alloc->>Alloc: Update trailing footer
                Alloc->>Alloc: header = prev
            end
        end

        Alloc->>Bins: free_list_insert(header)
        Alloc->>Mutex: pthread_mutex_unlock(&alloc_mutex)
        Alloc-->>App: Return
    end
```

---

## 4. Process Signal Dispatch Flow

This sequence demonstrates both local keyboard signal delivery and remote HTTP REST signal dispatch:

```mermaid
sequenceDiagram
    autonumber
    actor User as Engineer / Browser
    participant Client as TUI (tui.c) or Web UI (app.js)
    participant Server as HTTP Server (gui_server.c)
    participant Handler as signal_handler.c
    participant Kernel as Linux Kernel
    participant Process as Target Process

    alt Via Interactive Terminal UI (TUI)
        User->>Client: Press 'K' (SIGKILL), 's' (SIGSTOP), or 'c' (SIGCONT)
        Client->>Handler: signal_send_to_process(selected_pid, signal)
    else Via Web GUI Dashboard
        User->>Client: Click [STOP], [CONT], or [KILL] button in process row
        Client->>Server: POST /api/process/signal {"pid": 1234, "signal": "SIGTERM"}
        Server->>Handler: signal_parse_name("SIGTERM") -> SIGTERM (15)
        Server->>Handler: signal_send_to_process(1234, 15)
    end

    Handler->>Handler: Validate PID > 1 (prevent signaling kernel/init)
    Handler->>Kernel: kill(pid, signal)
    
    alt Permitted & Valid PID
        Kernel-->>Process: Deliver POSIX signal
        Kernel-->>Handler: Return 0 (Success)
        Handler-->>Client: Return 0
    else Permission Denied (EPERM) or Not Found (ESRCH)
        Kernel-->>Handler: Return -1 (errno set)
        Handler-->>Client: Log error & return errno
    end
```
