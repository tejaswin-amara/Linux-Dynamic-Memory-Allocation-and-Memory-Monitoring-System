#include "common.h"

int signal_send_to_process(pid_t pid, int sig) {
  if (pid <= 1) {
    LOG_ERROR("Refusing to dispatch signal to PID <= 1 (%d)", pid);
    return -1;
  }

  if (kill(pid, sig) != 0) {
    LOG_ERRNO_ERROR("kill(%d, %d) failed", pid, sig);
    return -1;
  }

  LOG_INFO("Successfully dispatched signal %d to PID %d", sig, pid);
  return 0;
}

int signal_parse_name(const char *name) {
  if (!name || strlen(name) == 0)
    return SIGTERM;
  if (strcmp(name, "SIGKILL") == 0 || strcmp(name, "9") == 0)
    return SIGKILL;
  if (strcmp(name, "SIGTERM") == 0 || strcmp(name, "15") == 0)
    return SIGTERM;
  if (strcmp(name, "SIGSTOP") == 0 || strcmp(name, "19") == 0)
    return SIGSTOP;
  if (strcmp(name, "SIGCONT") == 0 || strcmp(name, "18") == 0)
    return SIGCONT;
  if (strcmp(name, "SIGINT") == 0 || strcmp(name, "2") == 0)
    return SIGINT;
  if (strcmp(name, "0") == 0)
    return 0;

  char *endptr;
  long val = strtol(name, &endptr, 10);
  if (*endptr == '\0' && val >= 0 && val <= 31) {
    return (int)val;
  }

  return SIGTERM;
}
