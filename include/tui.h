#ifndef TUI_H
#define TUI_H

#include "common.h"
#include "proc_parser.h"

typedef enum {
  SORT_BY_CPU = 0,
  SORT_BY_MEM,
  SORT_BY_PID,
  SORT_BY_NAME
} sort_mode_t;

typedef struct {
  bool is_running;
  int selected_index;
  int scroll_offset;
  sort_mode_t sort_mode;
  char status_message[256];
  time_t status_message_expiry;
} tui_state_t;

int tui_init(void);
void tui_cleanup(void);
void tui_render(system_snapshot_t *snapshot, tui_state_t *state);
bool tui_handle_input(system_snapshot_t *snapshot, tui_state_t *state);

#endif /* TUI_H */
