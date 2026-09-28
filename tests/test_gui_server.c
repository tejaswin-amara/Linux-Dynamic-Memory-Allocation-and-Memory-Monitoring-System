#include "gui_server.h"
#include "unity.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

void setUp(void) {}
void tearDown(void) {}

static int http_request(const char *host, int port, const char *req, char *resp,
                        size_t max_resp) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, host, &addr.sin_addr);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }

  send(fd, req, strlen(req), 0);
  size_t total_read = 0;
  ssize_t n;
  while (total_read < max_resp - 1 &&
         (n = recv(fd, resp + total_read, max_resp - 1 - total_read, 0)) > 0) {
    total_read += (size_t)n;
  }
  close(fd);

  resp[total_read] = '\0';
  return 0;
}

void test_gui_server_routes_and_auth(void) {
  gui_server_t server;
  int port = 9876;
  int res = gui_server_init(&server, "127.0.0.1", port, "test-secret-token");
  TEST_ASSERT_EQUAL_INT(0, res);

  res = gui_server_start(&server);
  TEST_ASSERT_EQUAL_INT(0, res);

  system_snapshot_t *snap = calloc(1, sizeof(system_snapshot_t));
  TEST_ASSERT_NOT_NULL(snap);
  snap->timestamp = time(NULL);
  snap->count = 1;
  snap->procs[0].pid = getpid();
  strcpy(snap->procs[0].comm, "test_proc");
  snap->procs[0].state = 'R';
  snap->procs[0].cpu_usage_pct = 15.5f;
  snap->procs[0].vm_rss_kb = 2048;
  gui_server_update_snapshot(&server, snap);

  char resp[4096];

  /* 1. GET /api/metrics */
  res = http_request("127.0.0.1", port,
                     "GET /api/metrics HTTP/1.1\r\nHost: localhost\r\n\r\n",
                     resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "200 OK"));
  TEST_ASSERT_NOT_NULL(strstr(resp, "test_proc"));
  TEST_ASSERT_NOT_NULL(strstr(resp, "\"mem_total_kb\""));
  TEST_ASSERT_NOT_NULL(strstr(resp, "\"mem_available_kb\""));
  TEST_ASSERT_NOT_NULL(strstr(resp, "\"mem_usage_pct\""));
  TEST_ASSERT_NOT_NULL(strstr(resp, "\"swap_total_kb\""));
  TEST_ASSERT_NOT_NULL(strstr(resp, "\"swap_usage_pct\""));

  /* 2. POST /api/process/signal without token (401 Unauthorized) */
  char body_unauth[256];
  int body_unauth_len = snprintf(body_unauth, sizeof(body_unauth),
                                 "{\"pid\": %d, \"signal\": \"0\"}", getpid());
  char req_unauth[512];
  snprintf(
      req_unauth, sizeof(req_unauth),
      "POST /api/process/signal HTTP/1.1\r\nHost: localhost\r\nContent-Type: "
      "application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
      body_unauth_len, body_unauth);
  res = http_request("127.0.0.1", port, req_unauth, resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "401 Unauthorized"));

  /* 3. POST /api/process/signal with valid auth token */
  char body_auth[256];
  int body_auth_len = snprintf(body_auth, sizeof(body_auth),
                               "{\"pid\": %d, \"signal\": \"0\"}", getpid());
  char req_auth[512];
  snprintf(
      req_auth, sizeof(req_auth),
      "POST /api/process/signal HTTP/1.1\r\nHost: localhost\r\nX-Auth-Token: "
      "test-secret-token\r\nContent-Type: application/json\r\nContent-Length: "
      "%d\r\nConnection: close\r\n\r\n%s",
      body_auth_len, body_auth);
  res = http_request("127.0.0.1", port, req_auth, resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "200 OK"));

  /* 4. Bearer token auth */
  res = http_request("127.0.0.1", port,
                     "POST /api/process/signal HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer test-secret-token\r\nContent-Type: application/json\r\nContent-Length: 26\r\nConnection: close\r\n\r\n{\"pid\": 99999, \"signal\": \"0\"}",
                     resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "500 Internal Server Error"));

  /* 5. Invalid signal */
  res = http_request("127.0.0.1", port,
                     "POST /api/process/signal HTTP/1.1\r\nHost: localhost\r\nX-Auth-Token: test-secret-token\r\nContent-Type: application/json\r\nContent-Length: 34\r\nConnection: close\r\n\r\n{\"pid\": 99999, \"signal\": \"BOGUS\"}",
                     resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "400 Bad Request"));

  /* 6. Missing PID */
  res = http_request("127.0.0.1", port,
                     "POST /api/process/signal HTTP/1.1\r\nHost: localhost\r\nX-Auth-Token: test-secret-token\r\nContent-Type: application/json\r\nContent-Length: 17\r\nConnection: close\r\n\r\n{\"signal\": \"0\"}",
                     resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "400 Bad Request"));

  /* 7. Disconnect survival / 404 Route */
  res = http_request("127.0.0.1", port,
                     "GET /unknown_route HTTP/1.1\r\nHost: localhost\r\n\r\n",
                     resp, sizeof(resp));
  TEST_ASSERT_EQUAL_INT(0, res);
  TEST_ASSERT_NOT_NULL(strstr(resp, "404 Not Found"));

  free(snap);
  gui_server_stop(&server);
}

int main(void) {
  UnityBegin("test_gui_server.c");
  RUN_TEST(test_gui_server_routes_and_auth);
  return UnityEnd();
}
