#include "proc_parser.h"
#include <ctype.h>
#include <dirent.h>

static cpu_raw_jiffies_t prev_cpu_jiffies = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static int sys_core_count = 1;

int proc_parser_init(void) {
  long cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores > 0) {
    sys_core_count = (int)cores;
  }
  cpu_metrics_t dummy;
  return proc_parser_read_cpu(&dummy);
}

int proc_parser_read_cpu(cpu_metrics_t *metrics) {
  if (!metrics)
    return -1;

  int fd = open("/proc/stat", O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open /proc/stat");
    return -1;
  }

  char buf[1024];
  ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (bytes_read <= 0)
    return -1;
  buf[bytes_read] = '\0';

  cpu_raw_jiffies_t curr;
  memset(&curr, 0, sizeof(curr));

  int matched = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &curr.user, &curr.nice, &curr.system, &curr.idle,
                       &curr.iowait, &curr.irq, &curr.softirq, &curr.steal);

  if (matched < 4)
    return -1;

  curr.total = curr.user + curr.nice + curr.system + curr.idle + curr.iowait +
               curr.irq + curr.softirq + curr.steal;

  unsigned long long delta_total = curr.total - prev_cpu_jiffies.total;
  unsigned long long delta_idle = curr.idle - prev_cpu_jiffies.idle;
  unsigned long long delta_user = curr.user - prev_cpu_jiffies.user;
  unsigned long long delta_system = curr.system - prev_cpu_jiffies.system;

  metrics->core_count = sys_core_count;

  if (delta_total > 0) {
    metrics->total_usage_pct =
        ((float)(delta_total - delta_idle) / (float)delta_total) * 100.0f;
    metrics->user_pct = ((float)delta_user / (float)delta_total) * 100.0f;
    metrics->system_pct = ((float)delta_system / (float)delta_total) * 100.0f;
    metrics->idle_pct = ((float)delta_idle / (float)delta_total) * 100.0f;
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

  char buf[4096];
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
    unsigned long used = metrics->mem_total_kb - metrics->mem_available_kb;
    metrics->mem_usage_pct =
        ((float)used / (float)metrics->mem_total_kb) * 100.0f;
  }

  if (metrics->swap_total_kb > 0) {
    unsigned long swap_used = metrics->swap_total_kb - metrics->swap_free_kb;
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

  /* 1. Parse /proc/[pid]/stat */
  char path[MAX_PATH_LEN];
  snprintf(path, sizeof(path), "/proc/%d/stat", pid);

  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  char buf[2048];
  ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (bytes_read <= 0)
    return -1;
  buf[bytes_read] = '\0';

  char *comm_start = strchr(buf, '(');
  char *comm_end = strrchr(buf, ')');
  if (!comm_start || !comm_end || comm_end <= comm_start)
    return -1;

  size_t comm_len = comm_end - (comm_start + 1);
  if (comm_len >= MAX_COMM_LEN)
    comm_len = MAX_COMM_LEN - 1;
  strncpy(proc->comm, comm_start + 1, comm_len);
  proc->comm[comm_len] = '\0';

  char *rest = comm_end + 2;
  int ppid = 0;
  long priority = 0, nice = 0, num_threads = 0;
  unsigned long long utime = 0, stime = 0, starttime = 0;

  int matched = sscanf(rest,
                       "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu "
                       "%*d %*d %ld %ld %ld %*d %llu",
                       &proc->state, &ppid, &utime, &stime, &priority, &nice,
                       &num_threads, &starttime);

  if (matched >= 4) {
    proc->ppid = (pid_t)ppid;
    proc->utime = utime;
    proc->stime = stime;
    proc->total_time = utime + stime;
    proc->priority = priority;
    proc->nice = nice;
    proc->num_threads = num_threads;
    proc->starttime_ticks = starttime;

    if (total_system_jiffies_delta > 0) {
      proc->cpu_usage_pct =
          ((float)proc->total_time / (float)total_system_jiffies_delta) *
          100.0f;
    }
  }

  /* 2. Parse /proc/[pid]/status for VmSize, VmRSS, and context switches */
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

  proc_parser_read_cpu(&snapshot->cpu);
  proc_parser_read_mem(&snapshot->mem);

  DIR *dir = opendir("/proc");
  if (!dir) {
    LOG_ERROR("Failed to opendir /proc");
    return -1;
  }

  struct dirent *entry;
  int count = 0;

  while ((entry = readdir(dir)) != NULL && count < MAX_PROCS) {
    if (isdigit(entry->d_name[0])) {
      pid_t pid = (pid_t)atoi(entry->d_name);
      process_info_t proc;
      if (proc_parser_read_process(pid, &proc, 1000) == 0) {
        if (snapshot->mem.mem_total_kb > 0) {
          proc.mem_usage_pct =
              ((float)proc.vm_rss_kb / (float)snapshot->mem.mem_total_kb) *
              100.0f;
        }
        snapshot->procs[count++] = proc;
      }
    }
  }
  closedir(dir);
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
