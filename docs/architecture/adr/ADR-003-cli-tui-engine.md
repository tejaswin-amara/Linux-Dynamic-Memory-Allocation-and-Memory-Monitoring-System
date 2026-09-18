# ADR-003: Terminal User Interface (TUI) Architecture via ncurses

## Status
**Accepted** (Implemented in `src/ui/tui.c`)

## Context & Problem Statement
In systems operations and systems programming (KLEF 25CS2104E, Course Outcomes CO2 and CO3), systems engineers require an interactive, responsive terminal dashboard capable of:
1. Presenting high-density system health metrics (multi-core CPU gauges, RAM, Swap saturation).
2. Rendering a scrollable, sortable process table (supporting sorting by CPU%, Memory RSS, PID, and Process Name).
3. Receiving non-blocking keyboard input so that terminal rendering never blocks background telemetry updates.
4. Dispatching POSIX process signals (`SIGSTOP`, `SIGCONT`, `SIGKILL`) directly from the keyboard.
5. Reliably restoring terminal attributes (echoing, cursor visibility, raw mode) upon normal termination or asynchronous termination signals (`SIGINT`, `SIGTERM`).

## Decision Drivers
- **Terminal Portability**: Must operate seamlessly across Linux VT consoles, `xterm`, `gnome-terminal`, `kitty`, and `tmux`.
- **Non-blocking Input Processing**: Input collection must not freeze telemetry collection loops.
- **Flicker-Free Rendering**: Efficient differential terminal updates without full screen clear flickering.
- **Vim & Ergonomic Navigation**: Familiar keybindings (<kbd>j</kbd>/<kbd>k</kbd>, Arrow keys) for systems operators.
- **Clean Signal Handling & Teardown**: Preventing terminal corruption when the application is interrupted.

## Considered Alternatives
1. **Raw ANSI Escape Codes (`\033[...`)**:
   - *Pros*: Zero external library dependencies.
   - *Cons*: Highly fragile; hardcoded terminal assumption; complex terminal dimension detection (`TIOCGWINSZ`); manual dirty-rectangle tracking required to prevent severe screen flickering.
2. **Modern C++ TUI Frameworks (FTXUI / Notcurses)**:
   - *Pros*: Modern declarative UI components.
   - *Cons*: Violates C11 project constraint; introduces large C++ runtime dependencies; excessive abstraction.
3. **POSIX `ncurses` (Selected)**:
   - *Pros*: Standardized POSIX C library; native support for color pairs, keypad translation, and non-blocking input; optimized double-buffered differential rendering; lightweight memory footprint.

## Decision Outcome
We implemented the terminal dashboard using **POSIX `ncurses`** with non-blocking polling and dynamic color pairing:

### 1. Window Configuration & Terminal Modes
During `tui_init()`:
- `initscr()`: Initializes terminal data structures.
- `cbreak()`: Disables line buffering and passes character inputs immediately.
- `noecho()`: Suppresses character echo to maintain display cleanliness.
- `keypad(stdscr, TRUE)`: Enables function keys, keypad, and arrow key translations.
- `nodelay(stdscr, TRUE)`: Configures non-blocking `getch()` calls (returns `ERR` when no key is pending).
- `curs_set(0)`: Hides the hardware terminal cursor.

### 2. Color Palette & Alert Thresholds
Color pairs are initialized to provide intuitive visual hierarchy:
- `Pair 1 (CYAN / BLACK)`: Header borders, title text, and columnar table labels.
- `Pair 2 (GREEN / BLACK)`: Nominal resource usage ($< 50\%$).
- `Pair 3 (YELLOW / BLACK)`: Elevated resource consumption ($50\% - 80\%$).
- `Pair 4 (RED / BLACK)`: Critical resource threshold ($> 80\%$).
- `Pair 5 (BLACK / WHITE)`: Inverted highlight bar for the currently selected process row.

### 3. Non-Blocking Event Loop & Navigation
The main daemon loop executes on a 250 ms refresh cadence:
- Calls `tui_render()` to draw system gauges, sort indicator, and process table rows bounded by `getmaxyx(stdscr, rows, cols)`.
- Calls `tui_handle_input()` to process pending keystrokes:
  - <kbd>↓</kbd> or <kbd>j</kbd>: Move selection cursor down.
  - <kbd>↑</kbd> or <kbd>k</kbd>: Move selection cursor up.
  - <kbd>p</kbd> / <kbd>P</kbd>: Sort by differential CPU %.
  - <kbd>m</kbd> / <kbd>M</kbd>: Sort by Resident Set Size (RSS).
  - <kbd>s</kbd> / <kbd>S</kbd>: Dispatch `SIGSTOP` to highlighted PID.
  - <kbd>c</kbd> / <kbd>C</kbd>: Dispatch `SIGCONT` to highlighted PID.
  - <kbd>K</kbd> *(Shift+K)*: Dispatch `SIGKILL` to highlighted PID.
  - <kbd>q</kbd> / <kbd>Q</kbd>: Initiate clean exit.

### 4. Deterministic Cleanup & Restoration
`tui_cleanup()` guarantees that `endwin()` is called, resetting terminal attributes, unhiding the cursor, and restoring the cooked terminal buffer.

## Consequences

### Positive
- **High Responsiveness**: Non-blocking input loop ensures process monitoring continues uninterrupted.
- **Zero Terminal Drift**: Proper `endwin()` restoration prevents terminal shell state corruption on exit.
- **Lightweight Runtime**: Relies only on ubiquitous system `libncurses.so.6`.

### Negative
- **Terminal Resizing**: Terminal resize events (`KEY_RESIZE`) require refreshing geometry via `getmaxyx` to prevent table clipping.
- **Dependency**: Requires `libncurses-dev` at compile time.

## Verification & Compliance
- **Manual Verification**: Validated in standard GNOME Terminal, Alacritty, and tmux sessions.
- **Signal Resilience**: Verified clean teardown under `kill -SIGINT` and `kill -SIGTERM`.
