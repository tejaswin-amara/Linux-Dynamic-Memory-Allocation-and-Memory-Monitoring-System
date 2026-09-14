#ifndef GUI_SERVER_H
#define GUI_SERVER_H

#include "common.h"
#include "proc_parser.h"

typedef struct {
  int port;
  int server_fd;
  volatile bool is_running;
  pthread_t thread;
  pthread_rwlock_t snapshot_lock;
  system_snapshot_t latest_snapshot;
} gui_server_t;

int gui_server_init(gui_server_t *server, int port);
int gui_server_start(gui_server_t *server);
void gui_server_stop(gui_server_t *server);
void gui_server_update_snapshot(gui_server_t *server,
                                const system_snapshot_t *snapshot);

#endif /* GUI_SERVER_H */
