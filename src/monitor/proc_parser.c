#include "proc_parser.h"
#include <ctype.h>
#include <dirent.h>

static cpu_raw_jiffies_t prev_cpu_jiffies = {0};
static unsigned long long last_system_jiffies_delta = 0;
static int sys_core_count = 1;

typedef struct {
  pid_t pid;
  unsigned long long total_time;
} process_cpu_history_t;

static process_cpu_history_t prev_processes[MAX_PROCS];
static int prev_process_count = 0;

static unsigned long long previous_process_time(pid_t pid) {
  for (int i = 0; i < prev_process_count; ++i) {
    if (prev_processes[i].pid == pid)
      return prev_processes[i].total_time;
  }
  return 0;
}

int proc_parser_init(void) {
  long cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores > 0 && cores <= INT32_MAX)
    sys_core_count = (int)cores;

  cpu_metrics_t dummy;
  if (proc_parser_read_cpu(&dummy) != 0)
    return -1;

  prev_process_count = 0;
  memset(prev_processes, 0, sizeof(prev_processes));
  return 0;
}

int proc_parser_read_cpu(cpu_metrics_t *metrics) {
  if (!metrics)
    return -1;
  memset(metrics, 0, sizeof(*metrics));

  int fd = open("/proc/stat", O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open /proc/stat");
    return -1;
  }

  char buf[4096];
  ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (bytes_read <= 0)
    return -1;
  buf[bytes_read] = '\0';

  cpu_raw_jiffies_t curr = {0};
  int matched = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &curr.user, &curr.nice, &curr.system, &curr.idle,
                       &curr.iowait, &curr.irq, &curr.softirq, &curr.steal);
  if (matched < 4)
    return -1;

  curr.total = curr.user + curr.nice + curr.system + curr.idle + curr.iowait +
               curr.irq + curr.softirq + curr.steal;

  unsigned long long delta_total = curr.total >= prev_cpu_jiffies.total
                                        ? curr.total - prev_cpu_jiffies.total
                                        : 0;
  unsigned long long delta_idle = curr.idle >= prev_cpu_jiffies.idle
                                      ? curr.idle - prev_cpu_jiffies.idle
                                      : 0;
  unsigned long long delta_user = curr.user >= prev_cpu_jiffies.user
                                      ? curr.user - prev_cpu_jiffies.user
                                      : 0;
  unsigned long long delta_system = curr.system >= prev_cpu_jiffies.system
                                        ? curr.system - prev_cpu_jiffies.system
                                        : 0;

  metrics->core_count = sys_core_count;
  last_system_jiffies_delta = delta_total;

  if (delta_total > 0) {
    unsigned long long busy = delta_total > delta_idle
                                  ? delta_total - delta_idle
                                  : 0;
    metrics->total_usage_pct =
        ((float)busy / (float)delta_total) * 100.0f;
    metrics->user_pct =
        ((float)delta_user / (float)delta_total) * 100.0f;
    metrics->system_pct =
        ((float)delta_system / (float)delta_total) * 100.0f;
    metrics->idle_pct =
        ((float)delta_idle / (float)delta_total) * 100.0f;
  } else {
    metrics->total_usage_pct = 0.0f;
    metrics->user_pct = 0.0f;
    metrics->system_pct = 0.0f;
    metrics->idle_pct = 100.0f;
  }

  prev_cpu_jiffies = curr;
  return 0;
}

int proc_parser_read_mem(mem_metrics_t *metrics) {
  if (!metrics)
    return -1;
  memset(metrics, 0, sizeof(*metrics));

  int fd = open("/proc/meminfo", O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open /proc/meminfo");
    return -1;
  }

  char buf[8192];
  ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (bytes_read <= 0)
    return -1;
  buf[bytes_read] = '\0';

  char *line = strtok(buf, "\n");
  while (line) {
    unsigned long val = 0;
    if (sscanf(line, "MemTotal: %lu kB", &val) == 1)
      metrics->mem_total_kb = val;
    else if (sscanf(line, "MemFree: %lu kB", &val) == 1)
      metrics->mem_free_kb = val;
    else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1)
      metrics->mem_available_kb = val;
    else if (sscanf(line, "Buffers: %lu kB", &val) == 1)
      metrics->buffers_kb = val;
    else if (sscanf(line, "Cached: %lu kB", &val) == 1)
      metrics->cached_kb = val;
    else if (sscanf(line, "SwapTotal: %lu kB", &val) == 1)
      metrics->swap_total_kb = val;
    else if (sscanf(line, "SwapFree: %lu kB", &val) == 1)
      metrics->swap_free_kb = val;
    line = strtok(NULL, "\n");
  }

  if (metrics->mem_total_kb > 0) {
    unsigned long used = metrics->mem_total_kb > metrics->mem_available_kb
                              ? metrics->mem_total_kb - metrics->mem_available_kb
                              : 0;
    metrics->mem_usage_pct =
        ((float)used / (float)metrics->mem_total_kb) * 100.0f;
  }

  if (metrics->swap_total_kb > 0) {
    unsigned long swap_used = metrics->swap_total_kb > metrics->swap_free_kb
                                   ? metrics->swap_total_kb - metrics->swap_free_kb
                                   : 0;
    metrics->swap_usage_pct =
        ((float)swap_used / (float)metrics->swap_total_kb) * 100.0f;
  }

  return 0;
}

int proc_parser_read_process(pid_t pid, process_info_t *proc,
                             unsigned long long total_system_jiffies_delta) {
  if (!proc)
    return -1;
  memset(proc, 0, sizeof(*proc));
  proc->pid = pid;

  char path[MAX_PATH_LEN];
  snprintf(path, sizeof(path), "/proc/%d/stat", pid);

  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  char buf[4096];
  ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (bytes_read <= 0)
    return -1;
  buf[bytes_read] = '\0';

  char *comm_start = strchr(buf, '(');
  char *comm_end = strrchr(buf, ')');
  if (!comm_start || !comm_end || comm_end <= comm_start)
    return -1;

  size_t comm_len = (size_t)(comm_end - (comm_start + 1));
  if (comm_len >= MAX_COMM_LEN)
    comm_len = MAX_COMM_LEN - 1;
  memcpy(proc->comm, comm_start + 1, comm_len);
  proc->comm[comm_len] = '\0';

  char *rest = comm_end + 2;
  int ppid = 0;
  long priority = 0;
  long nice_value = 0;
  long num_threads = 0;
  unsigned long long utime = 0;
  unsigned long long stime = 0;
  unsigned long long starttime = 0;

  int matched = sscanf(
      rest,
      "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %ld %ld %ld %*d %llu",
      &proc->state, &ppid, &utime, &stime, &priority, &nice_value,
      &num_threads, &starttime);
  if (matched < 8)
    return -1;

  proc->ppid = (pid_t)ppid;
  proc->utime = utime;
  proc->stime = stime;
  proc->total_time = utime + stime;
  proc->priority = priority;
  proc->nice = nice_value;
  proc->num_threads = num_threads;
  proc->starttime_ticks = starttime;
  UNUSED(total_system_jiffies_delta);

  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  fd = open(path, O_RDONLY);
  if (fd >= 0) {
    bytes_read = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes_read > 0) {
      buf[bytes_read] = '\0';
      char *s_line = strtok(buf, "\n");
      while (s_line) {
        unsigned long val = 0;
        if (sscanf(s_line, "VmSize: %lu kB", &val) == 1)
          proc->vm_size_kb = val;
        else if (sscanf(s_line, "VmRSS: %lu kB", &val) == 1)
          proc->vm_rss_kb = val;
        else if (sscanf(s_line, "voluntary_ctxt_switches: %lu", &val) == 1)
          proc->voluntary_ctxt_switches = val;
        else if (sscanf(s_line, "nonvoluntary_ctxt_switches: %lu", &val) == 1)
          proc->nonvoluntary_ctxt_switches = val;
        s_line = strtok(NULL, "\n");
      }
    }
  }

  return 0;
}

int proc_parser_take_snapshot(system_snapshot_t *snapshot) {
  if (!snapshot)
    return -1;
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->timestamp = time(NULL);

  if (proc_parser_read_cpu(&snapshot->cpu) != 0 ||
      proc_parser_read_mem(&snapshot->mem) != 0)
    return -1;

  DIR *dir = opendir("/proc");
  if (!dir) {
    LOG_ERROR("Failed to opendir /proc");
    return -1;
  }

  struct dirent *entry;
  int count = 0;
  process_cpu_history_t current_processes[MAX_PROCS];
  int current_count = 0;
  memset(current_processes, 0, sizeof(current_processes));

  while ((entry = readdir(dir)) != NULL && count < MAX_PROCS) {
    if (!isdigit((unsigned char)entry->d_name[0]))
      continue;

    pid_t pid = (pid_t)atoi(entry->d_name);
    process_info_t proc;
    if (proc_parser_read_process(pid, &proc, last_system_jiffies_delta) != 0)
      continue;

    if (last_system_jiffies_delta > 0) {
      unsigned long long previous = previous_process_time(proc.pid);
      unsigned long long process_delta =
          proc.total_time >= previous ? proc.total_time - previous : 0;
      proc.cpu_usage_pct =
          ((float)process_delta / (float)last_system_jiffies_delta) *
          100.0f * (float)sys_core_count;
    }

    if (snapshot->mem.mem_total_kb > 0) {
      proc.mem_usage_pct =
          ((float)proc.vm_rss_kb / (float)snapshot->mem.mem_total_kb) * 100.0f;
    }

    snapshot->procs[count++] = proc;
    if (current_count < MAX_PROCS) {
      current_processes[current_count].pid = proc.pid;
      current_processes[current_count].total_time = proc.total_time;
      ++current_count;
    }
  }

  closedir(dir);
  memcpy(prev_processes, current_processes, sizeof(current_processes));
  prev_process_count = current_count;
  snapshot->count = count;
  return 0;
}

int proc_parser_get_maps(pid_t pid, char *buffer, size_t buf_size) {
  if (!buffer || buf_size == 0)
    return -1;

  char path[MAX_PATH_LEN];
  snprintf(path, sizeof(path), "/proc/%d/maps", pid);

  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  ssize_t bytes_read = read(fd, buffer, buf_size - 1);
  close(fd);
  if (bytes_read < 0)
    return -1;
  buffer[bytes_read] = '\0';
  return 0;
}
