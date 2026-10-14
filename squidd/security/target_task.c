//
//  target_task.c
//  Squid
//

#include "target_task.h"

#include <stdbool.h>
#include <unistd.h>

static mach_port_t retained_task = MACH_PORT_NULL;
static bool retained_task_owned;

kern_return_t target_task_acquire(pid_t pid)
{
    mach_port_t task = MACH_PORT_NULL;
    bool task_owned = false;
    kern_return_t result = KERN_SUCCESS;

    if (pid == getpid()) {
        task = mach_task_self();
    } else {
        result = task_for_pid(mach_task_self(), pid, &task);
        task_owned = result == KERN_SUCCESS;
    }

    if (result != KERN_SUCCESS) {
        return result;
    }

    if (retained_task != MACH_PORT_NULL && retained_task_owned) {
        mach_port_deallocate(mach_task_self(), retained_task);
    }

    retained_task = task;
    retained_task_owned = task_owned;
    return KERN_SUCCESS;
}

mach_port_t target_task_port(void)
{
    return retained_task;
}
