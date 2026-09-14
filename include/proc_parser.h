#ifndef PROC_PARSER_H
#define PROC_PARSER_H

#include "common.h"

#define MAX_COMM_LEN 256
#define MAX_PATH_LEN 512
#define MAX_PROCS 2048

/* Global CPU Utilization Metrics */
typedef struct {
  unsigned long long user;
  unsigned long long nice;
  unsigned long long system;
  unsigned long long idle;
  unsigned long long iowait;
  unsigned long long irq;
  unsigned long long softirq;
  unsigned long long steal;
  unsigned long long total;
} cpu_raw_jiffies_t;

typedef struct {
  float total_usage_pct;
  float user_pct;
  float system_pct;
  float idle_pct;
  int core_count;
} cpu_metrics_t;

/* Global RAM & Swap Metrics */
typedef struct {
  unsigned long mem_total_kb;
  unsigned long mem_free_kb;
  unsigned long mem_available_kb;
  unsigned long buffers_kb;
  unsigned long cached_kb;
  unsigned long swap_total_kb;
  unsigned long swap_free_kb;
  float mem_usage_pct;
  float swap_usage_pct;
} mem_metrics_t;

/* Per-Process Metrics */
typedef struct {
  pid_t pid;
  pid_t ppid;
  char comm[MAX_COMM_LEN];
  char state;
  unsigned long long utime;
  unsigned long long stime;
  unsigned long long total_time;
  float cpu_usage_pct;
  unsigned long vm_size_kb;
  unsigned long vm_rss_kb;
  unsigned long pss_kb;
  float mem_usage_pct;
  long priority;
  long nice;
  long num_threads;
  unsigned long long starttime_ticks;
  unsigned long voluntary_ctxt_switches;
  unsigned long nonvoluntary_ctxt_switches;
} process_info_t;

/* System Process Snapshot */
typedef struct {
  process_info_t procs[MAX_PROCS];
  int count;
  cpu_metrics_t cpu;
  mem_metrics_t mem;
  time_t timestamp;
} system_snapshot_t;

/* Parser API Prototypes */
int proc_parser_init(void);
int proc_parser_read_cpu(cpu_metrics_t *metrics);
int proc_parser_read_mem(mem_metrics_t *metrics);
int proc_parser_read_process(pid_t pid, process_info_t *proc,
                             unsigned long long total_system_jiffies_delta);
int proc_parser_take_snapshot(system_snapshot_t *snapshot);
int proc_parser_get_maps(pid_t pid, char *buffer, size_t buf_size);

#endif /* PROC_PARSER_H */
