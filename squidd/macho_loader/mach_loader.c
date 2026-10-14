#include "mach_loader.h"

#include "mach_o_parser.h"
#include "relocation.h"
#include "remote_vm.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool round_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false;
    }

    uint64_t mask = alignment - 1;
    if (value > UINT64_MAX - mask) {
        return false;
    }

    *result = (value + mask) & ~mask;
    return true;
}

static int loader_failure(const char *stage)
{
    fprintf(stderr, "[squid] Mach-O load failed during %s.\n", stage);
    return MACH_LOADER_ERROR;
}

static int write_remote_bytes(mach_port_t task,
                              uint64_t remote_address,
                              const uint8_t *bytes,
                              uint64_t size)
{
    uint64_t offset = 0;
    while (offset < size) {
        size_t chunk = size - offset > UINT_MAX
            ? UINT_MAX
            : (size_t)(size - offset);
        if (remote_vm_write(task,
                            remote_address + offset,
                            bytes + offset,
                            chunk) != 0) {
            return -1;
        }
        offset += chunk;
    }

    return 0;
}

static int copy_segments(mach_port_t task,
                         const mach_o_parser_context_t *parser,
                         const macho_image_layout_t *layout,
                         uint64_t remote_base,
                         uint64_t mapped_size)
{
    const uint8_t *cursor =
        parser->data + sizeof(struct mach_header_64);

    for (uint32_t index = 0; index < parser->header->ncmds; index++) {
        const struct load_command *command =
            (const struct load_command *)cursor;

        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            uint64_t segment_offset =
                segment->vmaddr - layout->minimum_vm_address;

            if (segment_offset > mapped_size ||
                segment->vmsize > mapped_size - segment_offset) {
                return -1;
            }

            if (segment->filesize != 0 &&
                write_remote_bytes(task,
                                   remote_base + segment_offset,
                                   parser->data + segment->fileoff,
                                   segment->filesize) != 0) {
                return -1;
            }
        }

        cursor += command->cmdsize;
    }

    return 0;
}

static int protect_segments(mach_port_t task,
                            const mach_o_parser_context_t *parser,
                            const macho_image_layout_t *layout,
                            uint64_t remote_base,
                            uint64_t mapped_size,
                            uint64_t page_size)
{
    if (remote_vm_protect(task,
                          remote_base,
                          mapped_size,
                          VM_PROT_NONE) != 0) {
        return -1;
    }

    const uint8_t *cursor =
        parser->data + sizeof(struct mach_header_64);
    for (uint32_t index = 0; index < parser->header->ncmds; index++) {
        const struct load_command *command =
            (const struct load_command *)cursor;

        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            if (segment->vmsize == 0) {
                cursor += command->cmdsize;
                continue;
            }

            uint64_t segment_offset =
                segment->vmaddr - layout->minimum_vm_address;
            uint64_t protection_start = segment_offset & ~(page_size - 1);
            uint64_t leading_bytes = segment_offset - protection_start;
            uint64_t protection_size = 0;
            if (segment->vmsize > UINT64_MAX - leading_bytes ||
                !round_up(segment->vmsize + leading_bytes,
                          page_size,
                          &protection_size) ||
                protection_start > mapped_size ||
                protection_size > mapped_size - protection_start) {
                return -1;
            }

            vm_prot_t protection = segment->initprot &
                (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
            if (remote_vm_protect(task,
                                  remote_base + protection_start,
                                  protection_size,
                                  protection) != 0) {
                return -1;
            }
        }

        cursor += command->cmdsize;
    }

    return 0;
}

int mach_loader_load(mach_port_t task,
                     const uint8_t *dylib_data,
                     size_t data_len,
                     mach_loader_context_t *ctx)
{
    if (task == MACH_PORT_NULL || dylib_data == NULL || ctx == NULL) {
        return loader_failure("argument validation");
    }

    *ctx = (mach_loader_context_t) { .task = task };

    mach_o_parser_context_t parser = { 0 };
    if (macho_validate(dylib_data, data_len, &parser) != 0) {
        return loader_failure("Mach-O validation");
    }

    int requires_fixups = macho_requires_dynamic_fixups(&parser);
    if (requires_fixups < 0) {
        return loader_failure("fixup metadata inspection");
    }

    macho_image_layout_t layout = { 0 };
    if (macho_get_image_layout(&parser, &layout) != 0) {
        return loader_failure("image layout calculation");
    }

    uint64_t page_size = (uint64_t)getpagesize();
    uint64_t mapped_size = 0;
    if ((layout.minimum_vm_address & (page_size - 1)) != 0 ||
        !round_up(layout.mapped_size, page_size, &mapped_size)) {
        return loader_failure("image alignment validation");
    }

    mach_vm_address_t remote_base = 0;
    kern_return_t kr = mach_vm_allocate(task,
                                        &remote_base,
                                        mapped_size,
                                        VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        return loader_failure("remote image allocation");
    }

    ctx->remote_addr = remote_base;
    ctx->load_addr = remote_base;
    ctx->mapped_size = mapped_size;
    ctx->slide = remote_base - layout.minimum_vm_address;

    if (copy_segments(task,
                      &parser,
                      &layout,
                      remote_base,
                      mapped_size) != 0) {
        mach_loader_unload(ctx);
        return loader_failure("segment copying");
    }

    int fixup_result = apply_chained_fixups(task,
                                            &parser,
                                            &layout,
                                            remote_base,
                                            mapped_size);
    if (fixup_result < 0) {
        mach_loader_unload(ctx);
        return loader_failure("chained fixup application");
    }
    if (requires_fixups != 0 && fixup_result == 0) {
        mach_loader_unload(ctx);
        return MACH_LOADER_REQUIRES_FIXUPS;
    }

    if (
        protect_segments(task,
                         &parser,
                         &layout,
                         remote_base,
                         mapped_size,
                         page_size) != 0 ||
        macho_resolve_symbol(&parser,
                             "_squid_tweak_init",
                             ctx->slide,
                             &ctx->entry_point) != 0) {
        mach_loader_unload(ctx);
        return loader_failure("segment protection or entry-point resolution");
    }

    if (remote_vm_run_initializer(task, ctx->entry_point) != 0) {
        mach_loader_unload(ctx);
        return loader_failure("remote initializer call");
    }

    uint64_t diagnostic_address = 0;
    if (macho_resolve_symbol(&parser,
                             "_dnd_patch_init_status",
                             ctx->slide,
                             &diagnostic_address) == 0) {
        uint64_t diagnostic_status = 0;
        if (remote_vm_read(task,
                           diagnostic_address,
                           &diagnostic_status,
                           sizeof(diagnostic_status)) == 0) {
            printf("Payload initializer status: %llu\n",
                   (unsigned long long)diagnostic_status);
        } else {
            fprintf(stderr,
                    "[squid] could not read payload initializer status.\n");
        }
    }

    printf("Dylib mapped at 0x%llx (%llu bytes), entry point 0x%llx\n",
           (unsigned long long)ctx->remote_addr,
           (unsigned long long)ctx->mapped_size,
           (unsigned long long)ctx->entry_point);
    return MACH_LOADER_SUCCESS;
}

int mach_loader_unload(mach_loader_context_t *ctx)
{
    if (ctx == NULL || ctx->task == MACH_PORT_NULL ||
        ctx->remote_addr == 0 || ctx->mapped_size == 0) {
        return MACH_LOADER_ERROR;
    }

    kern_return_t kr = mach_vm_deallocate(ctx->task,
                                          ctx->remote_addr,
                                          ctx->mapped_size);
    if (kr != KERN_SUCCESS) {
        return MACH_LOADER_ERROR;
    }

    *ctx = (mach_loader_context_t) { 0 };
    return MACH_LOADER_SUCCESS;
}
