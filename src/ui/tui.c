#include "tui.h"
#include "signal_handler.h"
#include <curses.h>

int tui_init(void) {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  curs_set(0);

  if (has_colors()) {
    start_color();
    init_pair(1, COLOR_CYAN, COLOR_BLACK);   /* Headers / Accents */
    init_pair(2, COLOR_GREEN, COLOR_BLACK);  /* Normal / Healthy */
    init_pair(3, COLOR_YELLOW, COLOR_BLACK); /* Warning / Elevated */
    init_pair(4, COLOR_RED, COLOR_BLACK);    /* Critical / Alert */
    init_pair(5, COLOR_BLACK, COLOR_WHITE);  /* Selected row */
  }
  return 0;
}

void tui_cleanup(void) { endwin(); }

static int compare_procs(const void *a, const void *b, void *arg) {
  const process_info_t *p1 = (const process_info_t *)a;
  const process_info_t *p2 = (const process_info_t *)b;
  sort_mode_t mode = *(sort_mode_t *)arg;

  switch (mode) {
  case SORT_BY_CPU:
    if (p2->cpu_usage_pct > p1->cpu_usage_pct)
      return 1;
    if (p2->cpu_usage_pct < p1->cpu_usage_pct)
      return -1;
    return 0;
  case SORT_BY_MEM:
    if (p2->vm_rss_kb > p1->vm_rss_kb)
      return 1;
    if (p2->vm_rss_kb < p1->vm_rss_kb)
      return -1;
    return 0;
  case SORT_BY_PID:
    return (p1->pid - p2->pid);
  case SORT_BY_NAME:
    return strcmp(p1->comm, p2->comm);
  }
  return 0;
}

void tui_render(system_snapshot_t *snapshot, tui_state_t *state) {
  erase();
  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  /* 1. Header & System Gauges */
  attron(COLOR_PAIR(1) | A_BOLD);
  mvprintw(0, 2,
           "=== Linux Dynamic Memory Allocation & System Task Manager (KLEF "
           "25CS2104E) ===");
  attroff(COLOR_PAIR(1) | A_BOLD);

  /* CPU Meter */
  mvprintw(2, 2, "CPU [%3.1f%%] [", snapshot->cpu.total_usage_pct);
  int bar_width = cols > 60 ? 30 : 15;
  int filled = (int)((snapshot->cpu.total_usage_pct / 100.0f) * bar_width);
  for (int i = 0; i < bar_width; ++i) {
    if (i < filled) {
      attron(COLOR_PAIR(snapshot->cpu.total_usage_pct > 80 ? 4 : 2));
      addch('|');
      attroff(COLOR_PAIR(snapshot->cpu.total_usage_pct > 80 ? 4 : 2));
    } else {
      addch(' ');
    }
  }
  printw("] Cores: %d", snapshot->cpu.core_count);

  /* Memory Meter */
  mvprintw(3, 2, "MEM [%3.1f%%] [", snapshot->mem.mem_usage_pct);
  filled = (int)((snapshot->mem.mem_usage_pct / 100.0f) * bar_width);
  for (int i = 0; i < bar_width; ++i) {
    if (i < filled) {
      attron(COLOR_PAIR(snapshot->mem.mem_usage_pct > 80 ? 4 : 2));
      addch('|');
      attroff(COLOR_PAIR(snapshot->mem.mem_usage_pct > 80 ? 4 : 2));
    } else {
      addch(' ');
    }
  }
  printw("] %lu / %lu MB",
         (snapshot->mem.mem_total_kb - snapshot->mem.mem_available_kb) / 1024,
         snapshot->mem.mem_total_kb / 1024);

  /* Sort Snapshot */
  qsort_r(snapshot->procs, snapshot->count, sizeof(process_info_t),
          compare_procs, &state->sort_mode);

  /* Clamp selection and scroll_offset to snapshot bounds */
  if (snapshot->count <= 0) {
    state->selected_index = 0;
    state->scroll_offset = 0;
  } else {
    if (state->selected_index >= snapshot->count) {
      state->selected_index = snapshot->count - 1;
    }
    if (state->selected_index < 0) {
      state->selected_index = 0;
    }
  }

  int table_start = 5;
  int max_rows = rows - table_start - 3;
  if (max_rows < 1)
    max_rows = 1;

  if (state->selected_index < state->scroll_offset) {
    state->scroll_offset = state->selected_index;
  } else if (state->selected_index >= state->scroll_offset + max_rows) {
    state->scroll_offset = state->selected_index - max_rows + 1;
  }

  if (state->scroll_offset < 0) {
    state->scroll_offset = 0;
  }

  /* 2. Process Table Header */
  attron(COLOR_PAIR(1) | A_UNDERLINE);
  mvprintw(table_start, 2, "%-8s %-20s %-6s %-8s %-8s %-12s", "PID", "COMMAND",
           "STATE", "THREADS", "CPU %", "RSS (KB)");
  attroff(COLOR_PAIR(1) | A_UNDERLINE);

  /* 3. Process Rows */
  for (int i = 0; i < max_rows && (i + state->scroll_offset) < snapshot->count;
       ++i) {
    int idx = i + state->scroll_offset;
    process_info_t *p = &snapshot->procs[idx];

    if (idx == state->selected_index) {
      attron(COLOR_PAIR(5) | A_BOLD);
    }

    mvprintw(table_start + 1 + i, 2, "%-8d %-20.20s %-6c %-8ld %-8.1f %-12lu",
             p->pid, p->comm, p->state, p->num_threads, p->cpu_usage_pct,
             p->vm_rss_kb);

    if (idx == state->selected_index) {
      attroff(COLOR_PAIR(5) | A_BOLD);
    }
  }

  /* 4. Footer & Keybinds */
  if (state->status_message[0] != '\0' &&
      time(NULL) < state->status_message_expiry) {
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(rows - 2, 2, "STATUS: %s", state->status_message);
    attroff(COLOR_PAIR(3) | A_BOLD);
  }

  attron(COLOR_PAIR(1));
  mvprintw(
      rows - 1, 2,
      "[k] Kill  [s] Stop  [c] Cont  [p] Sort CPU  [m] Sort MEM  [q] Quit");
  attroff(COLOR_PAIR(1));

  refresh();
}

bool tui_handle_input(system_snapshot_t *snapshot, tui_state_t *state) {
  int ch = getch();
  if (ch == ERR)
    return true;

  if (state->confirm_pending) {
    if (time(NULL) >= state->status_message_expiry) {
      state->confirm_pending = false;
      state->pending_kill_pid = -1;
      snprintf(state->status_message, sizeof(state->status_message),
               "SIGKILL confirmation expired");
      state->status_message_expiry = time(NULL) + 3;
    } else {
      if (ch == 'y' || ch == 'Y') {
        pid_t target_pid = state->pending_kill_pid;
        bool found = false;
        for (int i = 0; i < snapshot->count; ++i) {
          if (snapshot->procs[i].pid == target_pid) {
            found = true;
            break;
          }
        }

        if (found) {
          if (signal_send_to_process(target_pid, SIGKILL) == 0) {
            snprintf(state->status_message, sizeof(state->status_message),
                     "Successfully sent SIGKILL to PID %d", target_pid);
          } else {
            snprintf(state->status_message, sizeof(state->status_message),
                     "Failed to send SIGKILL to PID %d", target_pid);
          }
        } else {
          snprintf(state->status_message, sizeof(state->status_message),
                   "PID %d no longer present in snapshot", target_pid);
        }
        state->status_message_expiry = time(NULL) + 3;
      } else {
        snprintf(state->status_message, sizeof(state->status_message),
                 "SIGKILL cancelled");
        state->status_message_expiry = time(NULL) + 3;
      }
      state->confirm_pending = false;
      state->pending_kill_pid = -1;
      return true;
    }
  }

  switch (ch) {
  case 'q':
  case 'Q':
    state->is_running = false;
    return false;
  case KEY_DOWN:
  case 'j':
    if (state->selected_index < snapshot->count - 1) {
      state->selected_index++;
    }
    break;
  case KEY_UP:
  case 'k':
    if (state->selected_index > 0) {
      state->selected_index--;
    }
    break;
  case 'p':
  case 'P':
    state->sort_mode = SORT_BY_CPU;
    snprintf(state->status_message, sizeof(state->status_message),
             "Sorting by CPU %%");
    state->status_message_expiry = time(NULL) + 3;
    break;
  case 'm':
  case 'M':
    state->sort_mode = SORT_BY_MEM;
    snprintf(state->status_message, sizeof(state->status_message),
             "Sorting by Memory (RSS)");
    state->status_message_expiry = time(NULL) + 3;
    break;
  case 's':
  case 'S':
    if (snapshot->count > 0 && state->selected_index < snapshot->count) {
      pid_t pid = snapshot->procs[state->selected_index].pid;
      if (signal_send_to_process(pid, SIGSTOP) == 0) {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Sent SIGSTOP to PID %d", pid);
      } else {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Failed to send SIGSTOP to PID %d", pid);
      }
      state->status_message_expiry = time(NULL) + 3;
    }
    break;
  case 'c':
  case 'C':
    if (snapshot->count > 0 && state->selected_index < snapshot->count) {
      pid_t pid = snapshot->procs[state->selected_index].pid;
      if (signal_send_to_process(pid, SIGCONT) == 0) {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Sent SIGCONT to PID %d", pid);
      } else {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Failed to send SIGCONT to PID %d", pid);
      }
      state->status_message_expiry = time(NULL) + 3;
    }
    break;
  case 'K':
    if (snapshot->count > 0 && state->selected_index < snapshot->count) {
      pid_t pid = snapshot->procs[state->selected_index].pid;
      state->pending_kill_pid = pid;
      snprintf(state->status_message, sizeof(state->status_message),
               "Confirm SIGKILL for PID %d? (y/n)", pid);
      state->status_message_expiry = time(NULL) + 10;
      state->confirm_pending = true;
    }
    break;
  default:
    break;
  }
  return true;
}
