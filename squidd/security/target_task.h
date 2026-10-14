//
//  target_task.h
//  Squid
//

#ifndef TARGET_TASK_H
#define TARGET_TASK_H

#include <mach/mach.h>
#include <sys/types.h>

kern_return_t target_task_acquire(pid_t pid);
mach_port_t target_task_port(void);

#endif
