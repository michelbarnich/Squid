//
//  remote_vm.h
//  Squid
//

#ifndef REMOTE_VM_H
#define REMOTE_VM_H

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stddef.h>
#include <stdint.h>

int remote_vm_write(mach_port_t task, uint64_t address,
                    const void *bytes, size_t size);

int remote_vm_read(mach_port_t task, uint64_t address,
                   void *bytes, size_t size);

int remote_vm_protect(mach_port_t task, uint64_t address,
                      size_t size, vm_prot_t protection);

// Resolve a symbol in the target process, whose shared-cache slide can differ
// from the loader's, and return its unauthenticated address for chained fixups.
int remote_vm_resolve_symbol(mach_port_t task,
                             const char *symbol_name,
                             uint64_t *address);

// Resolve several target symbols in one LLDB attach/detach cycle.
int remote_vm_resolve_symbols(mach_port_t task,
                              const char *const *symbol_names,
                              size_t symbol_count,
                              uint64_t *addresses);

// PAC-sign and write authenticated pointers in one target-side expression.
int remote_vm_sign_and_write_pointers(mach_port_t task,
                                      const uint64_t *slot_addresses,
                                      const uint64_t *values,
                                      const uint8_t *keys,
                                      const uint64_t *discriminators,
                                      size_t pointer_count);

// Ask LLDB to sign and call the mapped image's initializer in the target.
int remote_vm_run_initializer(mach_port_t task, uint64_t function);

#endif // REMOTE_VM_H
