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

static void print_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s [--headless] [--json] [--port PORT] [--bind ADDRESS] "
          "[--token TOKEN]\n",
          program);
}

static bool generate_random_token(char *out, size_t len) {
  static const char hex_chars[] = "0123456789abcdef";
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    unsigned char bytes[32];
    ssize_t n = read(fd, bytes, sizeof(bytes));
    close(fd);
    if (n == sizeof(bytes)) {
      size_t pos = 0;
      for (size_t i = 0; i < sizeof(bytes) && pos + 2 < len; i++) {
        out[pos++] = hex_chars[(bytes[i] >> 4) & 0x0F];
        out[pos++] = hex_chars[bytes[i] & 0x0F];
      }
      out[pos] = '\0';
      return true;
    }
  }
  out[0] = '\0';
  return false;
}

int main(int argc, char **argv) {
  int port = 8080;
  char bind_host[64] = "127.0.0.1";
  char auth_token[MAX_AUTH_TOKEN_LEN] = {0};
  bool token_provided = false;
  bool headless = false;
  bool output_json = false;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--headless") == 0) {
      headless = true;
    } else if (strcmp(argv[i], "--json") == 0) {
      output_json = true;
      headless = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "--bind") == 0 ||
               strcmp(argv[i], "--host") == 0 || strcmp(argv[i], "--token") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
      }
      const char *value = argv[++i];

      if (strcmp(argv[i - 1], "--port") == 0) {
        char *endptr;
        errno = 0;
        long val = strtol(value, &endptr, 10);
        if (errno != 0 || *endptr != '\0' || val <= 0 || val > 65535) {
          fprintf(stderr, "Invalid port specified: %s\n", value);
          return 2;
        }
        port = (int)val;
      } else if (strcmp(argv[i - 1], "--bind") == 0 ||
                 strcmp(argv[i - 1], "--host") == 0) {
        if (strlen(value) >= sizeof(bind_host)) {
          fprintf(stderr, "Bind address is too long.\n");
          return 2;
        }
        strcpy(bind_host, value);
      } else {
        if (strlen(value) >= sizeof(auth_token)) {
          fprintf(stderr, "Auth token is too long.\n");
          return 2;
        }
        strcpy(auth_token, value);
        token_provided = true;
      }
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 2;
    }
  }

  if (!token_provided || strlen(auth_token) == 0) {
    if (!generate_random_token(auth_token, sizeof(auth_token))) {
      LOG_ERROR("Unable to obtain cryptographically secure randomness for the HTTP auth token");
      return 1;
    }
    LOG_INFO("Generated auth token: %s (use --token to set your own)", auth_token);
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, NULL) != 0 ||
      sigaction(SIGTERM, &sa, NULL) != 0) {
    LOG_ERRNO_ERROR("Failed to install signal handlers");
    return 1;
  }

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
    if (proc_parser_take_snapshot(snapshot) != 0) {
      LOG_ERROR("Failed to collect system snapshot");
      free(snapshot);
      return 1;
    }
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
    gui_server_stop(&server);
    free(snapshot);
    return 1;
  }

  if (headless) {
    LOG_INFO("Running in headless mode at http://%s:%d/ (auth: enabled). Press "
             "Ctrl+C to terminate.",
             bind_host, port);
    while (g_running) {
      if (proc_parser_take_snapshot(snapshot) != 0) {
        LOG_WARN("System snapshot failed; retaining previous dashboard state");
      } else {
        gui_server_update_snapshot(&server, snapshot);
      }
      sleep(1);
    }
  } else {
    if (tui_init() != 0) {
      LOG_ERROR("Failed to initialize ncurses TUI");
      gui_server_stop(&server);
      free(snapshot);
      return 1;
    }
    tui_state_t state = {.is_running = true,
                         .selected_index = 0,
                         .scroll_offset = 0,
                         .sort_mode = SORT_BY_CPU,
                         .status_message = {0},
                         .status_message_expiry = 0,
                         .confirm_pending = false,
                         .pending_kill_pid = -1};

    while (g_running && state.is_running) {
      if (proc_parser_take_snapshot(snapshot) != 0) {
        LOG_WARN("System snapshot failed; skipping this TUI frame");
      } else {
        gui_server_update_snapshot(&server, snapshot);
        tui_render(snapshot, &state);
        tui_handle_input(snapshot, &state);
      }

      usleep(250000); /* 250 ms refresh loop */
    }
    tui_cleanup();
  }

  gui_server_stop(&server);
  free(snapshot);
  LOG_INFO("Task manager shutdown completed.");
  return 0;
}
