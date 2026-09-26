#include "common.h"
#include "gui_server.h"
#include "proc_parser.h"
#include "tui.h"
#include <signal.h>

static volatile sig_atomic_t g_running = 1;

static void sigint_handler(int sig) {
  UNUSED(sig);
  g_running = 0;
}

int main(int argc, char **argv) {
  int port = 8080;
  char bind_host[64] = "127.0.0.1";
  char auth_token[MAX_AUTH_TOKEN_LEN] = DEFAULT_AUTH_TOKEN;
  bool headless = false;
  bool output_json = false;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--headless") == 0) {
      headless = true;
    } else if (strcmp(argv[i], "--json") == 0) {
      output_json = true;
      headless = true;
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      char *endptr;
      errno = 0;
      long val = strtol(argv[++i], &endptr, 10);
      if (errno != 0 || *endptr != '\0' || val <= 0 || val > 65535) {
        fprintf(stderr, "Invalid port specified: %s\n", argv[i]);
        return 1;
      }
      port = (int)val;
    } else if ((strcmp(argv[i], "--bind") == 0 ||
                strcmp(argv[i], "--host") == 0) &&
               i + 1 < argc) {
      strncpy(bind_host, argv[++i], sizeof(bind_host) - 1);
      bind_host[sizeof(bind_host) - 1] = '\0';
    } else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
      strncpy(auth_token, argv[++i], sizeof(auth_token) - 1);
      auth_token[sizeof(auth_token) - 1] = '\0';
    }
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  signal(SIGPIPE, SIG_IGN);

  if (proc_parser_init() != 0) {
    LOG_ERROR("Failed to initialize proc parser");
    return 1;
  }

  system_snapshot_t *snapshot = malloc(sizeof(system_snapshot_t));
  if (!snapshot) {
    LOG_ERROR("Failed to allocate memory for system snapshot");
    return 1;
  }

  if (output_json) {
    proc_parser_take_snapshot(snapshot);
    printf("{\n  \"cpu\": {\"total_usage_pct\": %.2f, \"core_count\": %d},\n",
           snapshot->cpu.total_usage_pct, snapshot->cpu.core_count);
    printf("  \"mem\": {\"mem_total_kb\": %lu, \"mem_available_kb\": %lu, "
           "\"mem_usage_pct\": %.2f},\n",
           snapshot->mem.mem_total_kb, snapshot->mem.mem_available_kb,
           snapshot->mem.mem_usage_pct);
    printf("  \"process_count\": %d\n}\n", snapshot->count);
    free(snapshot);
    return 0;
  }

  gui_server_t server;
  if (gui_server_init(&server, bind_host, port, auth_token) != 0) {
    LOG_ERROR("Failed to initialize GUI server");
    free(snapshot);
    return 1;
  }

  if (gui_server_start(&server) != 0) {
    LOG_ERROR("Failed to start GUI server");
    free(snapshot);
    return 1;
  }

  if (headless) {
    LOG_INFO("Running in headless mode at http://%s:%d/ (token: %s). Press "
             "Ctrl+C to terminate.",
             bind_host, port, strlen(auth_token) > 0 ? auth_token : "<none>");
    while (g_running) {
      proc_parser_take_snapshot(snapshot);
      gui_server_update_snapshot(&server, snapshot);
      sleep(1);
    }
  } else {
    tui_init();
    tui_state_t state = {.is_running = true,
                         .selected_index = 0,
                         .scroll_offset = 0,
                         .sort_mode = SORT_BY_CPU,
                         .status_message = {0},
                         .status_message_expiry = 0,
                         .confirm_pending = false};

    while (g_running && state.is_running) {
      proc_parser_take_snapshot(snapshot);
      gui_server_update_snapshot(&server, snapshot);

      tui_render(snapshot, &state);
      tui_handle_input(snapshot, &state);

      usleep(250000); /* 250 ms refresh loop */
    }
    tui_cleanup();
  }

  gui_server_stop(&server);
  free(snapshot);
  LOG_INFO("Task manager shutdown completed.");
  return 0;
}
