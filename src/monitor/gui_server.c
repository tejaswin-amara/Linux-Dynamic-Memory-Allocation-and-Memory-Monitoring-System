#include "gui_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

extern int signal_send_to_process(pid_t pid, int sig);
extern int signal_parse_name(const char *name);

static void *gui_server_worker(void *arg);
static void handle_client(int client_fd, gui_server_t *server);
static void serve_file(int client_fd, const char *filepath,
                       const char *content_type);
static void serve_metrics_json(int client_fd, gui_server_t *server);

int gui_server_init(gui_server_t *server, int port) {
  if (!server)
    return -1;
  memset(server, 0, sizeof(*server));
  server->port = port > 0 ? port : 8080;
  server->server_fd = -1;
  server->is_running = false;
  pthread_rwlock_init(&server->snapshot_lock, NULL);
  return 0;
}

int gui_server_start(gui_server_t *server) {
  if (!server)
    return -1;

  server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->server_fd < 0) {
    LOG_ERROR("Failed to create HTTP socket");
    return -1;
  }

  int opt = 1;
  setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(server->port);

  if (bind(server->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERROR("Failed to bind HTTP socket to port %d", server->port);
    close(server->server_fd);
    server->server_fd = -1;
    return -1;
  }

  if (listen(server->server_fd, 10) < 0) {
    LOG_ERROR("Failed to listen on HTTP socket");
    close(server->server_fd);
    server->server_fd = -1;
    return -1;
  }

  server->is_running = true;
  if (pthread_create(&server->thread, NULL, gui_server_worker, server) != 0) {
    LOG_ERROR("Failed to launch GUI server thread");
    server->is_running = false;
    close(server->server_fd);
    server->server_fd = -1;
    return -1;
  }

  LOG_INFO("Web GUI Dashboard available at http://localhost:%d", server->port);
  return 0;
}

void gui_server_stop(gui_server_t *server) {
  if (!server || !server->is_running)
    return;
  server->is_running = false;
  if (server->server_fd >= 0) {
    close(server->server_fd);
    server->server_fd = -1;
  }
  pthread_join(server->thread, NULL);
  pthread_rwlock_destroy(&server->snapshot_lock);
}

void gui_server_update_snapshot(gui_server_t *server,
                                const system_snapshot_t *snapshot) {
  if (!server || !snapshot)
    return;
  pthread_rwlock_wrlock(&server->snapshot_lock);
  server->latest_snapshot = *snapshot;
  pthread_rwlock_unlock(&server->snapshot_lock);
}

static void *gui_server_worker(void *arg) {
  gui_server_t *server = (gui_server_t *)arg;
  while (server->is_running) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server->server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
      if (!server->is_running)
        break;
      continue;
    }
    handle_client(client_fd, server);
    close(client_fd);
  }
  return NULL;
}

static void handle_client(int client_fd, gui_server_t *server) {
  char req_buf[2048];
  ssize_t n = read(client_fd, req_buf, sizeof(req_buf) - 1);
  if (n <= 0)
    return;
  req_buf[n] = '\0';

  char method[16], path[256];
  if (sscanf(req_buf, "%15s %255s", method, path) != 2)
    return;

  if (strcmp(method, "GET") == 0) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
      serve_file(client_fd, "web/index.html", "text/html");
    } else if (strcmp(path, "/style.css") == 0) {
      serve_file(client_fd, "web/style.css", "text/css");
    } else if (strcmp(path, "/app.js") == 0) {
      serve_file(client_fd, "web/app.js", "application/javascript");
    } else if (strcmp(path, "/api/metrics") == 0) {
      serve_metrics_json(client_fd, server);
    } else {
      const char *not_found =
          "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
      write(client_fd, not_found, strlen(not_found));
    }
  } else if (strcmp(method, "POST") == 0 &&
             strcmp(path, "/api/process/signal") == 0) {
    char *body = strstr(req_buf, "\r\n\r\n");
    int pid = 0;
    char sig_str[32] = {0};
    if (body) {
      sscanf(body, "%*[^0-9]%d", &pid);
      char *sig_pos = strstr(body, "signal");
      if (sig_pos) {
        sscanf(sig_pos, "%*[^:]: \"%31[^\"]\"", sig_str);
      }
    }
    if (pid > 1) {
      int sig = signal_parse_name(sig_str);
      signal_send_to_process(pid, sig);
      const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: "
                       "application/json\r\n\r\n{\"status\":\"DISPATCHED\"}";
      write(client_fd, ok, strlen(ok));
    } else {
      const char *bad =
          "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nInvalid PID";
      write(client_fd, bad, strlen(bad));
    }
  }
}

static void serve_file(int client_fd, const char *filepath,
                       const char *content_type) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    const char *not_found =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
    write(client_fd, not_found, strlen(not_found));
    return;
  }

  struct stat st;
  fstat(fd, &st);

  char header[512];
  int header_len = snprintf(
      header, sizeof(header),
      "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
      content_type, (long)st.st_size);
  write(client_fd, header, header_len);

  char buf[4096];
  ssize_t bytes;
  while ((bytes = read(fd, buf, sizeof(buf))) > 0) {
    write(client_fd, buf, bytes);
  }
  close(fd);
}

static void serve_metrics_json(int client_fd, gui_server_t *server) {
  pthread_rwlock_rdlock(&server->snapshot_lock);
  system_snapshot_t snap = server->latest_snapshot;
  pthread_rwlock_unlock(&server->snapshot_lock);

  /* Allocate buffer for JSON streaming */
  size_t cap = 256 * 1024;
  char *json = malloc(cap);
  if (!json)
    return;

  int offset = snprintf(
      json, cap,
      "{\n"
      "  \"cpu\": {\"total_usage_pct\": %.2f, \"user_pct\": %.2f, "
      "\"system_pct\": %.2f, \"core_count\": %d},\n"
      "  \"mem\": {\"mem_total_kb\": %lu, \"mem_available_kb\": %lu, "
      "\"mem_usage_pct\": %.2f, \"swap_total_kb\": %lu, \"swap_free_kb\": %lu, "
      "\"swap_usage_pct\": %.2f},\n"
      "  \"processes\": [\n",
      snap.cpu.total_usage_pct, snap.cpu.user_pct, snap.cpu.system_pct,
      snap.cpu.core_count, snap.mem.mem_total_kb, snap.mem.mem_available_kb,
      snap.mem.mem_usage_pct, snap.mem.swap_total_kb, snap.mem.swap_free_kb,
      snap.mem.swap_usage_pct);

  int max_send = snap.count < 100 ? snap.count : 100;
  for (int i = 0; i < max_send; ++i) {
    process_info_t *p = &snap.procs[i];
    offset +=
        snprintf(json + offset, cap - offset,
                 "    {\"pid\": %d, \"ppid\": %d, \"comm\": \"%s\", \"state\": "
                 "\"%c\", \"num_threads\": %ld, \"cpu_usage_pct\": %.2f, "
                 "\"vm_rss_kb\": %lu}%s\n",
                 p->pid, p->ppid, p->comm, p->state, p->num_threads,
                 p->cpu_usage_pct, p->vm_rss_kb, i == max_send - 1 ? "" : ",");
    if ((size_t)offset >= cap - 1024)
      break;
  }

  snprintf(json + offset, cap - offset, "  ]\n}\n");

  size_t content_len = strlen(json);
  char header[256];
  int h_len = snprintf(header, sizeof(header),
                       "HTTP/1.1 200 OK\r\nContent-Type: "
                       "application/json\r\nAccess-Control-Allow-Origin: "
                       "*\r\nContent-Length: %zu\r\n\r\n",
                       content_len);

  write(client_fd, header, h_len);
  write(client_fd, json, content_len);

  free(json);
}
