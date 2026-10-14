//
//  mach_loader.h
//  Squid

#ifndef MACH_LOADER_H
#define MACH_LOADER_H

#include <stdint.h>
#include <mach/mach.h>
#include <mach-o/loader.h>
#include <sys/types.h>

// Context for the loaded dylib in remote memory
typedef struct {
    mach_port_t task;
    uint64_t remote_addr;
    uint64_t mapped_size;
    uint64_t entry_point;
    uint64_t load_addr;
    uint64_t slide;
} mach_loader_context_t;

enum {
    MACH_LOADER_SUCCESS = 0,
    MACH_LOADER_ERROR = -1,
    MACH_LOADER_REQUIRES_FIXUPS = -2
};

// Load a dylib into remote process memory and execute its entry point
int mach_loader_load(mach_port_t task,
                     const uint8_t *dylib_data,
                     size_t data_len,
                     mach_loader_context_t *ctx);

// Unload a mapped dylib from remote process memory.
int mach_loader_unload(mach_loader_context_t *ctx);

#endif // MACH_LOADER_H
