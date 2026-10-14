//
//  remote_vm.c
//  Squid
//

#include "remote_vm.h"

#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

static int target_pid(mach_port_t task, int *pid)
{
    if (task == MACH_PORT_NULL || pid == NULL ||
        pid_for_task(task, pid) != KERN_SUCCESS || *pid < 2) {
        return -1;
    }
    return 0;
}

static int run_lldb_expression(mach_port_t task, const char *expression)
{
    int pid = 0;
    char pid_text[32];
    if (expression == NULL || target_pid(task, &pid) != 0 ||
        snprintf(pid_text, sizeof(pid_text), "%d", pid) <= 0) {
        return -1;
    }

    char *arguments[] = {
        "/usr/bin/lldb", "--batch", "-p", pid_text,
        "-o", (char *)expression,
        "-o", "process detach",
        NULL
    };
    pid_t child = 0;
    int spawn_result = posix_spawn(&child, arguments[0], NULL, NULL,
                                   arguments, environ);
    if (spawn_result != 0) {
        fprintf(stderr, "[squid] could not launch LLDB: %s.\n",
                strerror(spawn_result));
        return -1;
    }

    int status = 0;
    pid_t wait_result;
    do {
        wait_result = waitpid(child, &status, 0);
    } while (wait_result < 0 && errno == EINTR);

    if (wait_result != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "[squid] LLDB expression failed%s.\n",
                wait_result < 0 ? ": waitpid error" : "");
        return -1;
    }
    return 0;
}

int remote_vm_write(mach_port_t task, uint64_t address,
                    const void *bytes, size_t size)
{
    if (size == 0) {
        return 0;
    }
    if (task == MACH_PORT_NULL || address == 0 || bytes == NULL) {
        return -1;
    }

    const uint8_t *cursor = bytes;
    while (size > 0) {
        mach_msg_type_number_t chunk = size > UINT_MAX
            ? UINT_MAX
            : (mach_msg_type_number_t)size;
        if (mach_vm_write(task, address, (vm_offset_t)cursor, chunk) !=
            KERN_SUCCESS) {
            return -1;
        }
        address += chunk;
        cursor += chunk;
        size -= chunk;
    }
    return 0;
}

int remote_vm_read(mach_port_t task, uint64_t address,
                   void *bytes, size_t size)
{
    if (size == 0) {
        return 0;
    }
    if (task == MACH_PORT_NULL || address == 0 || bytes == NULL) {
        return -1;
    }

    mach_vm_size_t bytes_read = 0;
    kern_return_t result = mach_vm_read_overwrite(task, address, size,
                                                  (mach_vm_address_t)bytes,
                                                  &bytes_read);
    return result == KERN_SUCCESS && bytes_read == size ? 0 : -1;
}

int remote_vm_protect(mach_port_t task, uint64_t address,
                      size_t size, vm_prot_t protection)
{
    if (task == MACH_PORT_NULL || address == 0 || size == 0) {
        return -1;
    }
    kern_return_t result = mach_vm_protect(task, address, size, FALSE,
                                           protection);
    return result == KERN_SUCCESS ? 0 : -1;
}

int remote_vm_resolve_symbols(mach_port_t task,
                              const char *const *symbol_names,
                              size_t symbol_count,
                              uint64_t *addresses)
{
    if (symbol_count == 0) {
        return 0;
    }
    if (task == MACH_PORT_NULL || symbol_names == NULL || addresses == NULL) {
        return -1;
    }

    for (size_t index = 0; index < symbol_count; index++) {
        const char *symbol_name = symbol_names[index];
        if (symbol_name == NULL || symbol_name[0] == '\0') {
            return -1;
        }
        for (const unsigned char *cursor = (const unsigned char *)symbol_name;
             *cursor != '\0'; cursor++) {
            bool valid = (*cursor >= 'a' && *cursor <= 'z') ||
                         (*cursor >= 'A' && *cursor <= 'Z') ||
                         (*cursor >= '0' && *cursor <= '9') ||
                         *cursor == '_' || *cursor == '$';
            if (!valid) {
                return -1;
            }
        }
    }

    if (symbol_count > SIZE_MAX / sizeof(uint64_t)) {
        return -1;
    }

    mach_vm_address_t result_address = 0;
    size_t result_size = symbol_count * sizeof(uint64_t);
    if (mach_vm_allocate(task, &result_address, sizeof(uint64_t),
                         VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        return -1;
    }

    if (symbol_count != 1) {
        mach_vm_deallocate(task, result_address, sizeof(uint64_t));
        result_address = 0;
        if (mach_vm_allocate(task, &result_address, result_size,
                             VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
            return -1;
        }
    }

    size_t expression_capacity = 128;
    for (size_t index = 0; index < symbol_count; index++) {
        expression_capacity += strlen(symbol_names[index]) + 160;
    }
    char *expression = calloc(expression_capacity, sizeof(char));
    if (expression == NULL) {
        mach_vm_deallocate(task, result_address, result_size);
        return -1;
    }

    size_t used = 0;
    int written = snprintf(
        expression,
        expression_capacity,
        "expression -l c -- ("
    );
    if (written <= 0 || (size_t)written >= expression_capacity) {
        free(expression);
        mach_vm_deallocate(task, result_address, result_size);
        return -1;
    }
    used = (size_t)written;

    for (size_t index = 0; index < symbol_count; index++) {
        written = snprintf(
            expression + used,
            expression_capacity - used,
            "%s(*(unsigned long long *)0x%llx = "
            "(unsigned long long)__builtin_ptrauth_strip("
            "(void *)dlsym((void *)-2, \"%s\"), 0), 0)",
            index == 0 ? "" : ", ",
            (unsigned long long)(result_address + index * sizeof(uint64_t)),
            symbol_names[index]
        );
        if (written <= 0 || (size_t)written >= expression_capacity - used) {
            free(expression);
            mach_vm_deallocate(task, result_address, result_size);
            return -1;
        }
        used += (size_t)written;
    }

    written = snprintf(expression + used, expression_capacity - used, ")");
    if (written <= 0 || (size_t)written >= expression_capacity - used) {
        free(expression);
        mach_vm_deallocate(task, result_address, result_size);
        return -1;
    }

    int result = run_lldb_expression(task, expression);
    free(expression);
    if (result == 0) {
        result = remote_vm_read(task, result_address, addresses, result_size);
    }
    mach_vm_deallocate(task, result_address, result_size);

    if (result != 0) {
        fprintf(stderr, "[squid] target symbol batch resolution failed.\n");
        return -1;
    }
    return 0;
}

int remote_vm_resolve_symbol(mach_port_t task,
                             const char *symbol_name,
                             uint64_t *address)
{
    return remote_vm_resolve_symbols(task, &symbol_name, 1, address);
}

int remote_vm_sign_and_write_pointers(mach_port_t task,
                                      const uint64_t *slot_addresses,
                                      const uint64_t *values,
                                      const uint8_t *keys,
                                      const uint64_t *discriminators,
                                      size_t pointer_count)
{
    if (pointer_count == 0) {
        return 0;
    }
    if (task == MACH_PORT_NULL || slot_addresses == NULL || values == NULL ||
        keys == NULL || discriminators == NULL) {
        return -1;
    }

    size_t expression_capacity = 128;
    for (size_t index = 0; index < pointer_count; index++) {
        if (keys[index] > 3) {
            return -1;
        }
        expression_capacity += 190;
    }

    static const char *key_names[] = {
        "0",
        "1",
        "2",
        "3"
    };
    char *expression = calloc(expression_capacity, sizeof(char));
    if (expression == NULL) {
        return -1;
    }

    size_t used = 0;
    int written = snprintf(expression,
                           expression_capacity,
                           "expression -l c -- (");
    if (written <= 0 || (size_t)written >= expression_capacity) {
        free(expression);
        return -1;
    }
    used = (size_t)written;

    for (size_t index = 0; index < pointer_count; index++) {
        written = snprintf(
            expression + used,
            expression_capacity - used,
            "%s(*(unsigned long long *)0x%llx = "
            "(unsigned long long)__builtin_ptrauth_sign_unauthenticated("
            "(void *)0x%llx, %s, 0x%llx), 0)",
            index == 0 ? "" : ", ",
            (unsigned long long)slot_addresses[index],
            (unsigned long long)values[index],
            key_names[keys[index]],
            (unsigned long long)discriminators[index]
        );
        if (written <= 0 || (size_t)written >= expression_capacity - used) {
            free(expression);
            return -1;
        }
        used += (size_t)written;
    }

    written = snprintf(expression + used, expression_capacity - used, ")");
    if (written <= 0 || (size_t)written >= expression_capacity - used) {
        free(expression);
        return -1;
    }

    int result = run_lldb_expression(task, expression);
    free(expression);
    return result;
}

int remote_vm_run_initializer(mach_port_t task, uint64_t function)
{
    if (task == MACH_PORT_NULL || function == 0) {
        return -1;
    }

    char expression[512];
    int length = snprintf(
        expression, sizeof(expression),
        "expression -l c -- ((void (*)())"
        "__builtin_ptrauth_sign_unauthenticated((void *)0x%llx, 0, 0))()",
        (unsigned long long)function
    );
    if (length <= 0 || (size_t)length >= sizeof(expression)) {
        return -1;
    }
    return run_lldb_expression(task, expression);
}
