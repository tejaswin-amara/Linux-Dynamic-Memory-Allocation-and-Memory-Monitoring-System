#include "gui_server.h"
#include "signal_handler.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#define BUFFER_SIZE 8192
#define INITIAL_JSON_CAP (1024 * 1024)

typedef struct {
  gui_server_t *server;
  int client_fd;
} client_conn_t;

static int send_all(int fd, const char *buf, size_t len) {
  size_t total_sent = 0;
  while (total_sent < len) {
    ssize_t sent = send(fd, buf + total_sent, len - total_sent, MSG_NOSIGNAL);
    if (sent <= 0) {
      if (sent < 0 &&
          (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      return -1;
    }
    total_sent += (size_t)sent;
  }
  return 0;
}

static void send_response(int client_fd, const char *status,
                          const char *content_type, const char *body,
                          const char *extra_headers) {
  char header[1024];
  size_t body_len = body ? strlen(body) : 0;
  int hlen = snprintf(header, sizeof(header),
                      "HTTP/1.1 %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "%s"
                      "\r\n",
                      status, content_type, body_len,
                      extra_headers ? extra_headers : "");
  if (hlen > 0) {
    send_all(client_fd, header, (size_t)hlen);
  }
  if (body_len > 0) {
    send_all(client_fd, body, body_len);
  }
}

static void serve_file(int client_fd, const char *filepath,
                       const char *content_type) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    send_response(client_fd, "404 Not Found", "text/plain", "404 Not Found",
                  NULL);
    return;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    close(fd);
    send_response(client_fd, "500 Internal Server Error", "text/plain",
                  "500 Internal Error", NULL);
    return;
  }

  char header[512];
  int hlen = snprintf(header, sizeof(header),
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %lld\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      content_type, (long long)st.st_size);

  if (hlen > 0 && send_all(client_fd, header, (size_t)hlen) == 0) {
    char buf[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
      if (send_all(client_fd, buf, (size_t)bytes_read) != 0) {
        break;
      }
    }
  }

  close(fd);
}

static void serve_metrics_json(gui_server_t *server, int client_fd) {
  pthread_rwlock_rdlock(&server->snapshot_lock);
  const system_snapshot_t *snap = server->latest_snapshot;

  size_t cap = INITIAL_JSON_CAP;
  char *json = malloc(cap);
  if (!json) {
    pthread_rwlock_unlock(&server->snapshot_lock);
    send_response(client_fd, "500 Internal Server Error", "text/plain",
                  "Out of Memory", NULL);
    return;
  }

  size_t offset = 0;
  int w = snprintf(
      json + offset, cap - offset,
      "{\n"
      "  \"timestamp\": %ld,\n"
      "  \"cpu\": {\"total_usage_pct\": %.2f, \"user_pct\": %.2f, "
      "\"system_pct\": %.2f, \"idle_pct\": %.2f, \"core_count\": %d},\n"
      "  \"mem\": {\"total_mb\": %lu, \"used_mb\": %lu, \"free_mb\": %lu, "
      "\"usage_pct\": %.2f},\n"
      "  \"processes\": [\n",
      (long)(snap ? snap->timestamp : 0),
      snap ? snap->cpu.total_usage_pct : 0.0f, snap ? snap->cpu.user_pct : 0.0f,
      snap ? snap->cpu.system_pct : 0.0f, snap ? snap->cpu.idle_pct : 0.0f,
      snap ? snap->cpu.core_count : 0,
      snap ? snap->mem.mem_total_kb / 1024 : 0UL,
      snap ? (snap->mem.mem_total_kb - snap->mem.mem_available_kb) / 1024 : 0UL,
      snap ? snap->mem.mem_available_kb / 1024 : 0UL,
      snap ? snap->mem.mem_usage_pct : 0.0f);

  if (w > 0)
    offset += (size_t)w;

  int count = snap ? snap->count : 0;
  for (int i = 0; i < count; ++i) {
    if (offset + 1024 > cap) {
      size_t new_cap = cap * 2;
      char *new_json = realloc(json, new_cap);
      if (!new_json)
        break;
      json = new_json;
      cap = new_cap;
    }

    const process_info_t *p = &snap->procs[i];
    w = snprintf(
        json + offset, cap - offset,
        "    {\"pid\": %d, \"comm\": \"%s\", \"state\": \"%c\", "
        "\"cpu_pct\": %.2f, \"mem_rss_kb\": %lu, \"threads\": %ld}%s\n",
        p->pid, p->comm, p->state, p->cpu_usage_pct, p->vm_rss_kb,
        p->num_threads, (i == count - 1) ? "" : ",");
    if (w > 0) {
      if (offset + (size_t)w < cap) {
        offset += (size_t)w;
      } else {
        offset = cap - 1;
      }
    }
  }

  pthread_rwlock_unlock(&server->snapshot_lock);

  if (offset + 32 <= cap) {
    w = snprintf(json + offset, cap - offset, "  ]\n}\n");
    if (w > 0 && offset + (size_t)w < cap) {
      offset += (size_t)w;
    }
  }

  send_response(client_fd, "200 OK", "application/json", json,
                "Access-Control-Allow-Origin: *\r\n");
  free(json);
}

static bool check_auth(gui_server_t *server, const char *buffer) {
  if (strlen(server->auth_token) == 0) {
    return true;
  }

  char expected_header[256];
  snprintf(expected_header, sizeof(expected_header), "X-Auth-Token: %s",
           server->auth_token);
  if (strstr(buffer, expected_header) != NULL) {
    return true;
  }

  char expected_bearer[256];
  snprintf(expected_bearer, sizeof(expected_bearer), "Authorization: Bearer %s",
           server->auth_token);
  if (strstr(buffer, expected_bearer) != NULL) {
    return true;
  }

  return false;
}

static void parse_json_field(const char *json, const char *key, char *out_val,
                             size_t max_len) {
  out_val[0] = '\0';
  char search_key[128];
  snprintf(search_key, sizeof(search_key), "\"%s\"", key);

  const char *pos = strstr(json, search_key);
  if (!pos)
    return;

  pos += strlen(search_key);
  while (*pos == ' ' || *pos == '\t' || *pos == ':')
    pos++;

  size_t idx = 0;
  if (*pos == '"') {
    pos++;
    while (*pos && *pos != '"' && idx < max_len - 1) {
      out_val[idx++] = *pos++;
    }
  } else {
    while (*pos &&
           (*pos == '-' || (*pos >= '0' && *pos <= '9') ||
            (*pos >= 'a' && *pos <= 'z') || (*pos >= 'A' && *pos <= 'Z')) &&
           idx < max_len - 1) {
      out_val[idx++] = *pos++;
    }
  }
  out_val[idx] = '\0';
}

static void handle_process_signal(gui_server_t *server, int client_fd,
                                  const char *buffer) {
  if (!check_auth(server, buffer)) {
    send_response(client_fd, "401 Unauthorized", "application/json",
                  "{\"error\": \"Unauthorized\"}", NULL);
    return;
  }

  const char *body = strstr(buffer, "\r\n\r\n");
  if (!body) {
    send_response(client_fd, "400 Bad Request", "application/json",
                  "{\"error\": \"Missing body\"}", NULL);
    return;
  }
  body += 4;

  char pid_str[32] = {0};
  char sig_str[32] = {0};
  parse_json_field(body, "pid", pid_str, sizeof(pid_str));
  parse_json_field(body, "signal", sig_str, sizeof(sig_str));

  if (pid_str[0] == '\0') {
    send_response(client_fd, "400 Bad Request", "application/json",
                  "{\"error\": \"Missing pid\"}", NULL);
    return;
  }

  pid_t target_pid = (pid_t)atoi(pid_str);
  int sig = signal_parse_name(sig_str[0] ? sig_str : "SIGTERM");

  if (signal_send_to_process(target_pid, sig) == 0) {
    send_response(client_fd, "200 OK", "application/json",
                  "{\"status\": \"success\"}", NULL);
  } else {
    send_response(
        client_fd, "500 Internal Server Error", "application/json",
        "{\"status\": \"error\", \"message\": \"Failed to dispatch signal\"}",
        NULL);
  }
}

static void *client_thread_worker(void *arg) {
  client_conn_t *conn = (client_conn_t *)arg;
  int client_fd = conn->client_fd;
  gui_server_t *server = conn->server;
  free(conn);

  char buffer[BUFFER_SIZE];
  ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read > 0) {
    buffer[bytes_read] = '\0';

    if (strncmp(buffer, "GET / ", 6) == 0 ||
        strncmp(buffer, "GET /index.html", 15) == 0) {
      serve_file(client_fd, "web/index.html", "text/html");
    } else if (strncmp(buffer, "GET /style.css", 14) == 0) {
      serve_file(client_fd, "web/style.css", "text/css");
    } else if (strncmp(buffer, "GET /app.js", 11) == 0) {
      serve_file(client_fd, "web/app.js", "application/javascript");
    } else if (strncmp(buffer, "GET /api/metrics", 16) == 0) {
      serve_metrics_json(server, client_fd);
    } else if (strncmp(buffer, "POST /api/process/signal", 24) == 0) {
      handle_process_signal(server, client_fd, buffer);
    } else {
      if (strncmp(buffer, "GET ", 4) == 0 || strncmp(buffer, "POST ", 5) == 0 ||
          strncmp(buffer, "PUT ", 4) == 0 ||
          strncmp(buffer, "DELETE ", 7) == 0) {
        send_response(client_fd, "404 Not Found", "application/json",
                      "{\"error\": \"Route not found\"}", NULL);
      } else {
        send_response(client_fd, "405 Method Not Allowed", "application/json",
                      "{\"error\": \"Method not allowed\"}", NULL);
      }
    }
  }

  close(client_fd);
  return NULL;
}

static void *gui_server_worker(void *arg) {
  gui_server_t *server = (gui_server_t *)arg;

  while (server->is_running) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server->server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      if (!server->is_running)
        break;
      LOG_ERRNO_ERROR("accept failed on HTTP server");
      continue;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    client_conn_t *conn = malloc(sizeof(client_conn_t));
    if (!conn) {
      close(client_fd);
      continue;
    }
    conn->server = server;
    conn->client_fd = client_fd;

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&thread, &attr, client_thread_worker, conn) != 0) {
      close(client_fd);
      free(conn);
    }
    pthread_attr_destroy(&attr);
  }

  return NULL;
}

int gui_server_init(gui_server_t *server, const char *host, int port,
                    const char *auth_token) {
  if (!server)
    return -1;
  memset(server, 0, sizeof(*server));

  strncpy(server->bind_host, (host && strlen(host) > 0) ? host : "127.0.0.1",
          sizeof(server->bind_host) - 1);
  server->port = port;
  strncpy(server->auth_token, auth_token ? auth_token : DEFAULT_AUTH_TOKEN,
          sizeof(server->auth_token) - 1);
  server->server_fd = -1;
  server->is_running = false;

  server->latest_snapshot = malloc(sizeof(system_snapshot_t));
  if (!server->latest_snapshot) {
    LOG_ERROR("Failed to allocate snapshot for GUI server");
    return -1;
  }
  memset(server->latest_snapshot, 0, sizeof(system_snapshot_t));

  if (pthread_rwlock_init(&server->snapshot_lock, NULL) != 0) {
    LOG_ERROR("Failed to init snapshot rwlock");
    free(server->latest_snapshot);
    return -1;
  }

  return 0;
}

int gui_server_start(gui_server_t *server) {
  if (!server)
    return -1;

  signal(SIGPIPE, SIG_IGN);

  server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->server_fd < 0) {
    LOG_ERRNO_ERROR("Failed to create HTTP socket");
    return -1;
  }

  int opt = 1;
  if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    LOG_ERRNO_ERROR("Failed to set SO_REUSEADDR");
  }

  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  setsockopt(server->server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server->port);

  if (inet_pton(AF_INET, server->bind_host, &addr.sin_addr) <= 0) {
    LOG_ERROR("Invalid bind address: %s", server->bind_host);
    close(server->server_fd);
    return -1;
  }

  if (bind(server->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERRNO_ERROR("Failed to bind HTTP socket to %s:%d", server->bind_host,
                    server->port);
    close(server->server_fd);
    return -1;
  }

  if (listen(server->server_fd, 128) < 0) {
    LOG_ERRNO_ERROR("Failed to listen on HTTP socket");
    close(server->server_fd);
    return -1;
  }

  server->is_running = true;
  if (pthread_create(&server->thread, NULL, gui_server_worker, server) != 0) {
    LOG_ERRNO_ERROR("Failed to launch GUI server thread");
    server->is_running = false;
    close(server->server_fd);
    return -1;
  }

  LOG_INFO("Dashboard HTTP server active at http://%s:%d/ (auth: %s)",
           server->bind_host, server->port,
           strlen(server->auth_token) > 0 ? "enabled" : "disabled");
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
  if (server->latest_snapshot) {
    free(server->latest_snapshot);
    server->latest_snapshot = NULL;
  }
}

void gui_server_update_snapshot(gui_server_t *server,
                                const system_snapshot_t *snapshot) {
  if (!server || !snapshot || !server->latest_snapshot)
    return;

  pthread_rwlock_wrlock(&server->snapshot_lock);
  memcpy(server->latest_snapshot, snapshot, sizeof(system_snapshot_t));
  pthread_rwlock_unlock(&server->snapshot_lock);
}
