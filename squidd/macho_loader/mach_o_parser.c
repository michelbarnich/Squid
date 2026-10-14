#include "mach_o_parser.h"

#include <limits.h>
#include <mach-o/nlist.h>
#include <stdbool.h>
#include <string.h>

static bool range_is_valid(size_t offset, size_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

static bool command_region(const mach_o_parser_context_t *ctx,
                           const uint8_t **commands,
                           const uint8_t **commands_end)
{
    if (ctx == NULL || ctx->header == NULL ||
        ctx->len < sizeof(struct mach_header_64) ||
        !range_is_valid(sizeof(struct mach_header_64),
                        ctx->header->sizeofcmds,
                        ctx->len)) {
        return false;
    }

    *commands = ctx->data + sizeof(struct mach_header_64);
    *commands_end = *commands + ctx->header->sizeofcmds;
    return true;
}

static const struct load_command *command_at(const uint8_t *cursor,
                                             const uint8_t *commands_end)
{
    size_t remaining = (size_t)(commands_end - cursor);
    if (remaining < sizeof(struct load_command)) {
        return NULL;
    }

    const struct load_command *command =
        (const struct load_command *)cursor;
    if (command->cmdsize < sizeof(*command) || command->cmdsize > remaining) {
        return NULL;
    }

    return command;
}

static bool segment_is_valid(const mach_o_parser_context_t *ctx,
                             const struct segment_command_64 *segment)
{
    if (segment->cmdsize < sizeof(*segment) ||
        segment->nsects >
            (segment->cmdsize - sizeof(*segment)) / sizeof(struct section_64) ||
        segment->filesize > segment->vmsize ||
        segment->vmaddr > UINT64_MAX - segment->vmsize ||
        segment->fileoff > SIZE_MAX ||
        segment->filesize > SIZE_MAX ||
        !range_is_valid((size_t)segment->fileoff,
                        (size_t)segment->filesize,
                        ctx->len)) {
        return false;
    }

    return true;
}

int macho_validate(const uint8_t *data, size_t len, mach_o_parser_context_t *ctx)
{
    if (data == NULL || ctx == NULL || len < sizeof(struct mach_header_64)) {
        return -1;
    }

    *ctx = (mach_o_parser_context_t) {
        .data = data,
        .len = len,
        .header = (struct mach_header_64 *)data
    };

    if (ctx->header->magic != MH_MAGIC_64 ||
        ctx->header->cputype != CPU_TYPE_ARM64 ||
        (ctx->header->filetype != MH_DYLIB &&
         ctx->header->filetype != MH_BUNDLE)) {
        return -1;
    }

    const uint8_t *cursor = NULL;
    const uint8_t *commands_end = NULL;
    if (!command_region(ctx, &cursor, &commands_end)) {
        return -1;
    }

    for (uint32_t index = 0; index < ctx->header->ncmds; index++) {
        const struct load_command *command = command_at(cursor, commands_end);
        if (command == NULL) {
            return -1;
        }

        if (command->cmd == LC_SEGMENT_64 &&
            !segment_is_valid(ctx,
                              (const struct segment_command_64 *)command)) {
            return -1;
        }
        cursor += command->cmdsize;
    }

    if (cursor != commands_end) {
        return -1;
    }

    ctx->valid = 1;
    return 0;
}

int macho_get_segments(mach_o_parser_context_t *ctx,
                       macho_segment_info_t *segments,
                       size_t max_segments)
{
    if (segments == NULL && max_segments != 0) {
        return -1;
    }

    const uint8_t *cursor = NULL;
    const uint8_t *commands_end = NULL;
    if (!command_region(ctx, &cursor, &commands_end)) {
        return -1;
    }

    size_t count = 0;
    for (uint32_t index = 0; index < ctx->header->ncmds; index++) {
        const struct load_command *command = command_at(cursor, commands_end);
        if (command == NULL) {
            return -1;
        }

        if (command->cmd == LC_SEGMENT_64) {
            if (count == max_segments) {
                break;
            }

            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            segments[count++] = (macho_segment_info_t) {
                .vmsize = segment->vmsize,
                .vmaddr = segment->vmaddr,
                .fileoff = segment->fileoff,
                .flags = segment->flags,
                .nsects = segment->nsects
            };
        }
        cursor += command->cmdsize;
    }

    return count > INT_MAX ? -1 : (int)count;
}

int macho_get_image_layout(mach_o_parser_context_t *ctx,
                           macho_image_layout_t *layout)
{
    if (ctx == NULL || !ctx->valid || layout == NULL) {
        return -1;
    }

    const uint8_t *cursor = NULL;
    const uint8_t *commands_end = NULL;
    if (!command_region(ctx, &cursor, &commands_end)) {
        return -1;
    }

    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0;
    bool found_segment = false;

    for (uint32_t index = 0; index < ctx->header->ncmds; index++) {
        const struct load_command *command = command_at(cursor, commands_end);
        if (command == NULL) {
            return -1;
        }

        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            if (segment->vmsize != 0) {
                uint64_t end = segment->vmaddr + segment->vmsize;
                if (segment->vmaddr < minimum) {
                    minimum = segment->vmaddr;
                }
                if (end > maximum) {
                    maximum = end;
                }
                found_segment = true;
            }
        }
        cursor += command->cmdsize;
    }

    if (!found_segment || maximum <= minimum) {
        return -1;
    }

    *layout = (macho_image_layout_t) {
        .minimum_vm_address = minimum,
        .maximum_vm_address = maximum,
        .mapped_size = maximum - minimum
    };
    return 0;
}

int macho_calculate_memory_layout(mach_o_parser_context_t *ctx,
                                  uint64_t *total_size,
                                  uint64_t *max_vaddr)
{
    if (total_size == NULL || max_vaddr == NULL) {
        return -1;
    }

    macho_image_layout_t layout = { 0 };
    if (macho_get_image_layout(ctx, &layout) != 0) {
        return -1;
    }

    *total_size = layout.mapped_size;
    *max_vaddr = layout.maximum_vm_address;
    return 0;
}

int macho_resolve_symbol(mach_o_parser_context_t *ctx,
                         const char *symbol_name,
                         uint64_t slide,
                         uint64_t *address)
{
    if (ctx == NULL || !ctx->valid || symbol_name == NULL ||
        symbol_name[0] == '\0' || address == NULL) {
        return -1;
    }

    const struct symtab_command *symtab = NULL;
    const uint8_t *cursor = NULL;
    const uint8_t *commands_end = NULL;
    if (!command_region(ctx, &cursor, &commands_end)) {
        return -1;
    }

    for (uint32_t index = 0; index < ctx->header->ncmds; index++) {
        const struct load_command *command = command_at(cursor, commands_end);
        if (command == NULL) {
            return -1;
        }
        if (command->cmd == LC_SYMTAB &&
            command->cmdsize >= sizeof(struct symtab_command)) {
            symtab = (const struct symtab_command *)command;
            break;
        }
        cursor += command->cmdsize;
    }

    if (symtab == NULL ||
        !range_is_valid(symtab->symoff,
                        (size_t)symtab->nsyms * sizeof(struct nlist_64),
                        ctx->len) ||
        !range_is_valid(symtab->stroff, symtab->strsize, ctx->len)) {
        return -1;
    }

    const struct nlist_64 *symbols =
        (const struct nlist_64 *)(ctx->data + symtab->symoff);
    const char *strings = (const char *)(ctx->data + symtab->stroff);

    for (uint32_t index = 0; index < symtab->nsyms; index++) {
        uint32_t string_index = symbols[index].n_un.n_strx;
        if (string_index == 0 || string_index >= symtab->strsize ||
            (symbols[index].n_type & N_STAB) != 0 ||
            (symbols[index].n_type & N_TYPE) == N_UNDF) {
            continue;
        }

        const char *name = strings + string_index;
        size_t remaining = symtab->strsize - string_index;
        if (memchr(name, '\0', remaining) == NULL ||
            strcmp(name, symbol_name) != 0 ||
            symbols[index].n_value > UINT64_MAX - slide) {
            continue;
        }

        *address = symbols[index].n_value + slide;
        return 0;
    }

    return -1;
}

int macho_requires_dynamic_fixups(mach_o_parser_context_t *ctx)
{
    if (ctx == NULL || !ctx->valid) {
        return -1;
    }

    const uint8_t *cursor = NULL;
    const uint8_t *commands_end = NULL;
    if (!command_region(ctx, &cursor, &commands_end)) {
        return -1;
    }

    for (uint32_t index = 0; index < ctx->header->ncmds; index++) {
        const struct load_command *command = command_at(cursor, commands_end);
        if (command == NULL) {
            return -1;
        }

        if (command->cmd == LC_DYLD_CHAINED_FIXUPS) {
            return 1;
        }

        if ((command->cmd == LC_DYLD_INFO ||
             command->cmd == LC_DYLD_INFO_ONLY) &&
            command->cmdsize >= sizeof(struct dyld_info_command)) {
            const struct dyld_info_command *info =
                (const struct dyld_info_command *)command;
            if (info->rebase_size != 0 || info->bind_size != 0 ||
                info->weak_bind_size != 0 || info->lazy_bind_size != 0) {
                return 1;
            }
        }
        cursor += command->cmdsize;
    }

    return 0;
}
