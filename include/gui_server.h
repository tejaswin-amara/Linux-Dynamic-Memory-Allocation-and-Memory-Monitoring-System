#ifndef GUI_SERVER_H
#define GUI_SERVER_H

#include "common.h"
#include "proc_parser.h"

#define MAX_AUTH_TOKEN_LEN 128
#define MAX_CLIENT_WORKERS 32

typedef struct {
  char bind_host[64];
  int port;
  char auth_token[MAX_AUTH_TOKEN_LEN];
  int server_fd;
  volatile bool is_running;
  pthread_t thread;
  pthread_rwlock_t snapshot_lock;
  system_snapshot_t *latest_snapshot;
  _Atomic int active_workers;
  bool initialized;
  pthread_mutex_t worker_mutex;
  pthread_cond_t worker_cond;
} gui_server_t;

int gui_server_init(gui_server_t *server, const char *host, int port,
                    const char *auth_token);
int gui_server_start(gui_server_t *server);
void gui_server_stop(gui_server_t *server);
void gui_server_update_snapshot(gui_server_t *server,
                                const system_snapshot_t *snapshot);

#endif /* GUI_SERVER_H */
