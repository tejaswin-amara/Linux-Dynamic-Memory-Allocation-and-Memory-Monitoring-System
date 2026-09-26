#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include "common.h"

int signal_send_to_process(pid_t pid, int sig);
int signal_parse_name(const char *name);

#endif /* SIGNAL_HANDLER_H */
