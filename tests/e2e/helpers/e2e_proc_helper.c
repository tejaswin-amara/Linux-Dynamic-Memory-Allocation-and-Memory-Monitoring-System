#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "proc_parser.h"

int main(int argc, char **argv) {
    if (proc_parser_init() != 0) {
        fprintf(stderr, "proc_parser_init failed\n");
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <cpu|mem|snapshot>\n", argv[0]);
        return 2;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "cpu") == 0) {
        cpu_metrics_t cpu;
        if (proc_parser_read_cpu(&cpu) != 0) return 3;
        printf("{\"total_usage_pct\": %.2f, \"core_count\": %d}\n",
               cpu.total_usage_pct, cpu.core_count);
        return 0;
    } else if (strcmp(cmd, "mem") == 0) {
        mem_metrics_t mem;
        if (proc_parser_read_mem(&mem) != 0) return 4;
        printf("{\"mem_total_kb\": %lu, \"mem_available_kb\": %lu, \"mem_usage_pct\": %.2f}\n",
               mem.mem_total_kb, mem.mem_available_kb, mem.mem_usage_pct);
        return 0;
    } else if (strcmp(cmd, "snapshot") == 0) {
        system_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (proc_parser_take_snapshot(&snap) != 0) return 5;
        printf("{\"count\": %d, \"first_pid\": %d, \"first_comm\": \"%s\", \"first_state\": \"%c\"}\n",
               snap.count,
               snap.count > 0 ? snap.procs[0].pid : 0,
               snap.count > 0 ? snap.procs[0].comm : "",
               snap.count > 0 ? snap.procs[0].state : ' ');
        return 0;
    }

    return 2;
}
