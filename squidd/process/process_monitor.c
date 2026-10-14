//
//  process_monitor.c
//  Squid
//

#include "process_monitor.h"

#include <libproc.h>
#include <stdlib.h>

static int compare_processes(const void *left_value, const void *right_value)
{
    const process_identity_t *left = left_value;
    const process_identity_t *right = right_value;

    if (left->pid != right->pid) {
        return left->pid < right->pid ? -1 : 1;
    }

    if (left->start_seconds != right->start_seconds) {
        return left->start_seconds < right->start_seconds ? -1 : 1;
    }

    if (left->start_microseconds != right->start_microseconds) {
        return left->start_microseconds < right->start_microseconds ? -1 : 1;
    }

    return 0;
}

static pid_t *copy_all_pids(int *pid_count)
{
    int estimated_count = proc_listallpids(NULL, 0);
    if (estimated_count <= 0) {
        return NULL;
    }

    int capacity = estimated_count + 32;
    pid_t *pids = NULL;

    for (;;) {
        pid_t *larger_buffer = realloc(
            pids,
            (size_t)capacity * sizeof(*pids)
        );
        if (larger_buffer == NULL) {
            free(pids);
            return NULL;
        }

        pids = larger_buffer;
        *pid_count = proc_listallpids(
            pids,
            capacity * (int)sizeof(*pids)
        );

        if (*pid_count < 0) {
            free(pids);
            return NULL;
        }

        if (*pid_count < capacity) {
            return pids;
        }

        capacity *= 2;
    }
}

bool process_snapshot_capture(process_snapshot_t *snapshot)
{
    *snapshot = (process_snapshot_t) { 0 };

    int pid_count = 0;
    pid_t *pids = copy_all_pids(&pid_count);
    if (pids == NULL) {
        return false;
    }

    process_identity_t *processes = NULL;
    if (pid_count > 0) {
        processes = calloc((size_t)pid_count, sizeof(*processes));
        if (processes == NULL) {
            free(pids);
            return false;
        }
    }

    for (int index = 0; index < pid_count; index++) {
        if (pids[index] <= 0) {
            continue;
        }

        struct proc_bsdinfo info = { 0 };
        int bytes_read = proc_pidinfo(
            pids[index],
            PROC_PIDTBSDINFO,
            0,
            &info,
            (int)sizeof(info)
        );

        if (bytes_read != sizeof(info)) {
            continue;
        }

        processes[snapshot->count] = (process_identity_t) {
            .pid = pids[index],
            .start_seconds = info.pbi_start_tvsec,
            .start_microseconds = info.pbi_start_tvusec
        };
        snapshot->count++;
    }

    free(pids);

    if (snapshot->count > 1) {
        qsort(
            processes,
            snapshot->count,
            sizeof(*processes),
            compare_processes
        );
    }

    snapshot->processes = processes;
    return true;
}

bool process_snapshot_contains(
    const process_snapshot_t *snapshot,
    const process_identity_t *process
)
{
    if (snapshot->count == 0) {
        return false;
    }

    return bsearch(
        process,
        snapshot->processes,
        snapshot->count,
        sizeof(*snapshot->processes),
        compare_processes
    ) != NULL;
}

void process_snapshot_destroy(process_snapshot_t *snapshot)
{
    free(snapshot->processes);
    *snapshot = (process_snapshot_t) { 0 };
}
