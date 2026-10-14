//
//  process_monitor.h
//  Squid
//

#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    pid_t pid;
    uint64_t start_seconds;
    uint64_t start_microseconds;
} process_identity_t;

typedef struct {
    process_identity_t *processes;
    size_t count;
} process_snapshot_t;

bool process_snapshot_capture(process_snapshot_t *snapshot);
bool process_snapshot_contains(
    const process_snapshot_t *snapshot,
    const process_identity_t *process
);
void process_snapshot_destroy(process_snapshot_t *snapshot);

#endif
