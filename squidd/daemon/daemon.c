#include "daemon.h"
#include "../macho_loader/mach_loader.h"
#include "../process/process_monitor.h"
#include "../security/code_identity.h"
#include "../security/target_task.h"
#include "../setup/setup.h"

#include <fcntl.h>
#include <libproc.h>
#include <mach/mach_error.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const long POLL_INTERVAL_NANOSECONDS = 50000000L;

static void *map_payload_file(const char *path, size_t *file_size)
{
    *file_size = 0;

    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return MAP_FAILED;
    }

    struct stat status = { 0 };
    if (fstat(descriptor, &status) != 0 || status.st_size <= 0 ||
        (uintmax_t)status.st_size > SIZE_MAX) {
        close(descriptor);
        return MAP_FAILED;
    }

    void *image = mmap(NULL,
                       (size_t)status.st_size,
                       PROT_READ,
                       MAP_PRIVATE,
                       descriptor,
                       0);
    close(descriptor);
    if (image != MAP_FAILED) {
        *file_size = (size_t)status.st_size;
    }
    return image;
}

static void prepare_target_process(
    pid_t pid,
    const tweak_config_t *tweak
)
{
    kern_return_t result = target_task_acquire(pid);
    if (result != KERN_SUCCESS) {
        fprintf(
            stderr,
            "task_for_pid failed for target PID=%d (%s): %s\n",
            pid,
            tweak->identifier,
            mach_error_string(result)
        );
        return;
    }

    size_t file_size = 0;
    void *image = map_payload_file(tweak->library_path, &file_size);
    if (image == MAP_FAILED) {
        fprintf(stderr,
                "Could not map payload %s for target PID=%d.\n",
                tweak->library_path,
                pid);
        return;
    }

    mach_loader_context_t loaded_image = { 0 };
    int load_result = mach_loader_load(target_task_port(),
                                       image,
                                       file_size,
                                       &loaded_image);
    munmap(image, file_size);

    if (load_result == MACH_LOADER_SUCCESS) {
        printf("Loaded tweak %s (%s) into PID=%d at 0x%llx.\n",
               tweak->identifier,
               tweak->library_path,
               pid,
               (unsigned long long)loaded_image.remote_addr);
    } else if (load_result == MACH_LOADER_REQUIRES_FIXUPS) {
        fprintf(stderr,
                "Tweak %s uses an unsupported fixup format.\n",
                tweak->identifier);
    } else {
        fprintf(stderr,
                "Could not initialize tweak %s in PID=%d.\n",
                tweak->identifier,
                pid);
    }

    fflush(stdout);
}

static void handle_new_process(const process_identity_t *process)
{
    char identifier[PROC_PIDPATHINFO_MAXSIZE] = { 0 };
    bool has_identifier = copy_process_signing_identifier(process->pid,
                                                           identifier,
                                                           sizeof(identifier));

    printf("New Process: PID=%d bundleID=%s\n",
           process->pid,
           has_identifier ? identifier : "(none)");
    fflush(stdout);

    if (!has_identifier) {
        return;
    }

    const tweak_config_store_t *store = tweak_configs();
    for (size_t index = 0; index < store->count; index++) {
        const tweak_config_t *tweak = &store->tweaks[index];
        if (tweak_config_matches(
                tweak,
                identifier,
                current_os_version())) {
            prepare_target_process(process->pid, tweak);
        }
    }
}

void run_loop(void)
{
    process_snapshot_t previous;
    if (!process_snapshot_capture(&previous)) {
        fprintf(stderr, "Could not read the initial process list.\n");
        return;
    }

    for (size_t index = 0; index < previous.count; index++) {
        handle_new_process(&previous.processes[index]);
    }

    const struct timespec poll_interval = {
        .tv_sec = 0,
        .tv_nsec = POLL_INTERVAL_NANOSECONDS
    };

    size_t configuration_refresh_ticks = 0;
    for (;;) {
        nanosleep(&poll_interval, NULL);

        if (++configuration_refresh_ticks >= 20) {
            if (!reload_tweak_configs()) {
                fprintf(stderr, "Could not refresh Squid tweak configuration.\\n");
            }
            configuration_refresh_ticks = 0;
        }

        process_snapshot_t current;
        if (!process_snapshot_capture(&current)) {
            fprintf(stderr, "Could not refresh the process list.\n");
            continue;
        }

        for (size_t index = 0; index < current.count; index++) {
            if (!process_snapshot_contains(&previous,
                                           &current.processes[index])) {
                handle_new_process(&current.processes[index]);
            }
        }

        process_snapshot_destroy(&previous);
        previous = current;
    }
}
