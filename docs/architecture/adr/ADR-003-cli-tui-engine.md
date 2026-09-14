# ADR-003: Terminal User Interface (TUI) Architecture via ncurses

## Status
Accepted

## Context
Systems engineers require an interactive, responsive terminal dashboard capable of sorting processes, displaying high-density visual meters (CPU cores, memory usage, swap), and dispatching POSIX process signals (`SIGSTOP`, `SIGCONT`, `SIGTERM`, `SIGKILL`) in real-time.

## Decision
1. **ncurses Library**:
   - Utilize POSIX `ncurses` for terminal window management, color pairing, and cursor addressing.
   - Enable `nodelay(stdscr, TRUE)` and `cbreak()` for non-blocking key event processing, ensuring terminal rendering never blocks background metric sampling.
2. **Display Sections**:
   - **Header Window**: ASCII meters for global CPU cores, RAM, and Swap utilization.
   - **Process Table Window**: Columnar display of PID, User, State, Threads, Memory (RSS/VSize), CPU%, and Process Name. Supports cursor navigation and sorting toggles.
   - **Footer Window**: Interactive keybind status line displaying available commands (`k` kill, `s` stop, `c` continue, `p` sort cpu, `m` sort mem, `q` quit).
3. **Signal Dispatching**:
   - The TUI directly interfaces with `signal_handler.c` to dispatch signals to the currently highlighted PID using `kill(2)` and display operation feedback (`SUCCESS` or error code).

## Consequences
- Lightweight, zero-dependency binary runtime (beyond standard `libncurses`).
- Clean terminal restoration on exit or signal interruption (`SIGINT`/`SIGTERM`) via `endwin()`.
