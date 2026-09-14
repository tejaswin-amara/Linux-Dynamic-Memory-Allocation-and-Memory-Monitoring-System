#include "common.h"
#include "gui_server.h"
#include "proc_parser.h"
#include "tui.h"

static volatile bool g_running = true;

static void sigint_handler(int sig) {
  UNUSED(sig);
  g_running = false;
}

int main(int argc, char **argv) {
  int port = 8080;
  bool headless = false;
  bool output_json = false;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--headless") == 0) {
      headless = true;
    } else if (strcmp(argv[i], "--json") == 0) {
      output_json = true;
      headless = true;
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    }
  }

  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigint_handler);

  proc_parser_init();

  if (output_json) {
    system_snapshot_t snapshot;
    proc_parser_take_snapshot(&snapshot);
    printf("{\n  \"cpu\": {\"total_usage_pct\": %.2f, \"core_count\": %d},\n",
           snapshot.cpu.total_usage_pct, snapshot.cpu.core_count);
    printf("  \"mem\": {\"mem_total_kb\": %lu, \"mem_available_kb\": %lu, "
           "\"mem_usage_pct\": %.2f},\n",
           snapshot.mem.mem_total_kb, snapshot.mem.mem_available_kb,
           snapshot.mem.mem_usage_pct);
    printf("  \"process_count\": %d\n}\n", snapshot.count);
    return 0;
  }

  gui_server_t server;
  gui_server_init(&server, port);
  gui_server_start(&server);

  if (headless) {
    LOG_INFO("Running in headless mode on port %d. Press Ctrl+C to terminate.",
             port);
    while (g_running) {
      system_snapshot_t snapshot;
      proc_parser_take_snapshot(&snapshot);
      gui_server_update_snapshot(&server, &snapshot);
      sleep(1);
    }
  } else {
    tui_init();
    tui_state_t state = {.is_running = true,
                         .selected_index = 0,
                         .scroll_offset = 0,
                         .sort_mode = SORT_BY_CPU,
                         .status_message = {0},
                         .status_message_expiry = 0};

    while (g_running && state.is_running) {
      system_snapshot_t snapshot;
      proc_parser_take_snapshot(&snapshot);
      gui_server_update_snapshot(&server, &snapshot);

      tui_render(&snapshot, &state);
      tui_handle_input(&snapshot, &state);

      usleep(250000); /* 250 ms refresh loop */
    }
    tui_cleanup();
  }

  gui_server_stop(&server);
  LOG_INFO("Task manager shutdown completed.");
  return 0;
}
