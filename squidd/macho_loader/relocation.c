#include "relocation.h"

#include "remote_vm.h"

#include <limits.h>
#include <mach-o/fixup-chains.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t address;
    int64_t addend;
} resolved_import_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    const struct dyld_chained_fixups_header *header;
} chained_fixups_t;

typedef struct {
    uint64_t slot_address;
    uint64_t value;
    uint64_t discriminator;
    uint8_t key;
} authenticated_fixup_t;

static int append_authenticated_fixup(authenticated_fixup_t **fixups,
                                      size_t *count,
                                      uint64_t slot_address,
                                      uint64_t value,
                                      uint8_t key,
                                      uint64_t discriminator)
{
    if (*count == SIZE_MAX / sizeof(**fixups)) {
        return -1;
    }
    authenticated_fixup_t *resized = realloc(
        *fixups,
        (*count + 1) * sizeof(**fixups)
    );
    if (resized == NULL) {
        return -1;
    }
    resized[*count] = (authenticated_fixup_t) {
        .slot_address = slot_address,
        .value = value,
        .discriminator = discriminator,
        .key = key
    };
    *fixups = resized;
    (*count)++;
    return 0;
}

static bool range_is_valid(size_t offset, size_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

static bool add_signed(uint64_t value, int64_t addend, uint64_t *result)
{
    if (addend >= 0) {
        uint64_t positive = (uint64_t)addend;
        if (value > UINT64_MAX - positive) {
            return false;
        }
        *result = value + positive;
        return true;
    }

    uint64_t magnitude = (uint64_t)(-(addend + 1)) + 1;
    if (value < magnitude) {
        return false;
    }
    *result = value - magnitude;
    return true;
}

static int find_chained_fixups(const mach_o_parser_context_t *parser,
                               chained_fixups_t *fixups)
{
    const uint8_t *cursor =
        parser->data + sizeof(struct mach_header_64);

    for (uint32_t index = 0; index < parser->header->ncmds; index++) {
        const struct load_command *command =
            (const struct load_command *)cursor;
        if (command->cmd == LC_DYLD_CHAINED_FIXUPS) {
            if (command->cmdsize < sizeof(struct linkedit_data_command)) {
                return -1;
            }

            const struct linkedit_data_command *linkedit =
                (const struct linkedit_data_command *)command;
            if (!range_is_valid(linkedit->dataoff,
                                linkedit->datasize,
                                parser->len) ||
                linkedit->datasize <
                    sizeof(struct dyld_chained_fixups_header)) {
                return -1;
            }

            fixups->data = parser->data + linkedit->dataoff;
            fixups->size = linkedit->datasize;
            fixups->header =
                (const struct dyld_chained_fixups_header *)fixups->data;
            return 1;
        }
        cursor += command->cmdsize;
    }

    return 0;
}

static bool copy_import(const chained_fixups_t *fixups,
                        uint32_t index,
                        uint32_t *name_offset,
                        bool *weak_import,
                        int64_t *addend)
{
    const struct dyld_chained_fixups_header *header = fixups->header;
    size_t entry_size = 0;
    switch (header->imports_format) {
        case DYLD_CHAINED_IMPORT:
            entry_size = sizeof(struct dyld_chained_import);
            break;
        case DYLD_CHAINED_IMPORT_ADDEND:
            entry_size = sizeof(struct dyld_chained_import_addend);
            break;
        case DYLD_CHAINED_IMPORT_ADDEND64:
            entry_size = sizeof(struct dyld_chained_import_addend64);
            break;
        default:
            return false;
    }

    if (index > (SIZE_MAX - header->imports_offset) / entry_size) {
        return false;
    }
    size_t offset = header->imports_offset + (size_t)index * entry_size;
    if (!range_is_valid(offset, entry_size, fixups->size)) {
        return false;
    }

    if (header->imports_format == DYLD_CHAINED_IMPORT) {
        const struct dyld_chained_import *entry =
            (const struct dyld_chained_import *)(fixups->data + offset);
        *name_offset = entry->name_offset;
        *weak_import = entry->weak_import;
        *addend = 0;
    } else if (header->imports_format == DYLD_CHAINED_IMPORT_ADDEND) {
        const struct dyld_chained_import_addend *entry =
            (const struct dyld_chained_import_addend *)(fixups->data + offset);
        *name_offset = entry->name_offset;
        *weak_import = entry->weak_import;
        *addend = entry->addend;
    } else {
        const struct dyld_chained_import_addend64 *entry =
            (const struct dyld_chained_import_addend64 *)(fixups->data + offset);
        *name_offset = entry->name_offset;
        *weak_import = entry->weak_import;
        *addend = (int64_t)entry->addend;
    }

    return true;
}

static int resolve_imports(mach_port_t task,
                           const chained_fixups_t *fixups,
                           resolved_import_t **resolved_imports)
{
    const struct dyld_chained_fixups_header *header = fixups->header;
    if (header->symbols_format != 0 ||
        header->symbols_offset >= fixups->size) {
        return -1;
    }

    resolved_import_t *imports = calloc(header->imports_count,
                                         sizeof(*imports));
    if (imports == NULL && header->imports_count != 0) {
        return -1;
    }

    const char **symbol_names = calloc(header->imports_count,
                                       sizeof(*symbol_names));
    bool *weak_imports = calloc(header->imports_count,
                                sizeof(*weak_imports));
    int64_t *addends = calloc(header->imports_count, sizeof(*addends));
    uint64_t *addresses = calloc(header->imports_count, sizeof(*addresses));
    if ((symbol_names == NULL || weak_imports == NULL || addends == NULL ||
         addresses == NULL) && header->imports_count != 0) {
        free(imports);
        free(symbol_names);
        free(weak_imports);
        free(addends);
        free(addresses);
        return -1;
    }

    for (uint32_t index = 0; index < header->imports_count; index++) {
        uint32_t name_offset = 0;
        bool weak_import = false;
        int64_t addend = 0;
        if (!copy_import(fixups,
                         index,
                         &name_offset,
                         &weak_import,
                         &addend) ||
            name_offset > SIZE_MAX - header->symbols_offset) {
            free(imports); free(symbol_names); free(weak_imports);
            free(addends); free(addresses);
            return -1;
        }

        size_t symbol_offset = header->symbols_offset + name_offset;
        if (symbol_offset >= fixups->size) {
            free(imports); free(symbol_names); free(weak_imports);
            free(addends); free(addresses);
            return -1;
        }

        const char *symbol = (const char *)(fixups->data + symbol_offset);
        size_t remaining = fixups->size - symbol_offset;
        if (memchr(symbol, '\0', remaining) == NULL) {
            free(imports); free(symbol_names); free(weak_imports);
            free(addends); free(addresses);
            return -1;
        }

        // Mach-O external C symbols carry a leading underscore, while dlsym
        // accepts the source-level spelling.
        symbol_names[index] = symbol[0] == '_' ? symbol + 1 : symbol;
        weak_imports[index] = weak_import;
        addends[index] = addend;
    }

    if (remote_vm_resolve_symbols(task,
                                  symbol_names,
                                  header->imports_count,
                                  addresses) != 0) {
        free(imports); free(symbol_names); free(weak_imports);
        free(addends); free(addresses);
        return -1;
    }

    for (uint32_t index = 0; index < header->imports_count; index++) {
        if (addresses[index] == 0 && !weak_imports[index]) {
            free(imports); free(symbol_names); free(weak_imports);
            free(addends); free(addresses);
            return -1;
        }
        imports[index] = (resolved_import_t) {
            .address = addresses[index],
            .addend = addends[index]
        };
    }

    free(symbol_names);
    free(weak_imports);
    free(addends);
    free(addresses);

    *resolved_imports = imports;
    return 0;
}

static int fix_pointer_chain(mach_port_t task,
                             uint64_t remote_base,
                             uint64_t mapped_size,
                             uint16_t pointer_format,
                             uint64_t chain_offset,
                             uint64_t page_end,
                             const resolved_import_t *imports,
                             uint32_t imports_count,
                             uint64_t slide,
                             authenticated_fixup_t **authenticated_fixups,
                             size_t *authenticated_fixup_count)
{
    for (;;) {
        if (chain_offset > mapped_size - sizeof(uint64_t) ||
            chain_offset + sizeof(uint64_t) > page_end) {
            return -1;
        }

        uint64_t raw = 0;
        if (remote_vm_read(task,
                           remote_base + chain_offset,
                           &raw,
                           sizeof(raw)) != 0) {
            return -1;
        }

        uint64_t fixed = 0;
        uint16_t next = 0;
        bool arm64e = pointer_format == DYLD_CHAINED_PTR_ARM64E ||
                      pointer_format == DYLD_CHAINED_PTR_ARM64E_USERLAND ||
                      pointer_format == DYLD_CHAINED_PTR_ARM64E_USERLAND24;
        bool userland24 =
            pointer_format == DYLD_CHAINED_PTR_ARM64E_USERLAND24;
        bool authenticated = arm64e && ((raw >> 63) != 0);
        bool bind = arm64e ? ((raw >> 62) & 1) != 0 : ((raw >> 63) != 0);
        uint8_t auth_key = 0;
        uint64_t auth_discriminator = 0;
        if (arm64e && bind) {
            if (authenticated) {
                uint32_t ordinal = 0;
                uint16_t diversity = 0;
                bool addr_div = false;
                uint8_t key = 0;
                uint16_t chain_next = 0;
                if (userland24) {
                    struct dyld_chained_ptr_arm64e_auth_bind24 pointer = { 0 };
                    memcpy(&pointer, &raw, sizeof(pointer));
                    ordinal = pointer.ordinal;
                    diversity = pointer.diversity;
                    addr_div = pointer.addrDiv;
                    key = pointer.key;
                    chain_next = pointer.next;
                } else {
                    struct dyld_chained_ptr_arm64e_auth_bind pointer = { 0 };
                    memcpy(&pointer, &raw, sizeof(pointer));
                    ordinal = pointer.ordinal;
                    diversity = pointer.diversity;
                    addr_div = pointer.addrDiv;
                    key = pointer.key;
                    chain_next = pointer.next;
                }
                if (ordinal >= imports_count) {
                    return -1;
                }
                const resolved_import_t *import = &imports[ordinal];
                if (!add_signed(import->address,
                                import->addend,
                                &fixed)) {
                    return -1;
                }
                auth_key = key;
                auth_discriminator = addr_div
                    ? remote_base + chain_offset
                    : diversity;
                next = chain_next;
            } else {
                uint32_t ordinal = 0;
                uint32_t encoded_addend = 0;
                uint16_t chain_next = 0;
                if (userland24) {
                    struct dyld_chained_ptr_arm64e_bind24 pointer = { 0 };
                    memcpy(&pointer, &raw, sizeof(pointer));
                    ordinal = pointer.ordinal;
                    encoded_addend = pointer.addend;
                    chain_next = pointer.next;
                } else {
                    struct dyld_chained_ptr_arm64e_bind pointer = { 0 };
                    memcpy(&pointer, &raw, sizeof(pointer));
                    ordinal = pointer.ordinal;
                    encoded_addend = pointer.addend;
                    chain_next = pointer.next;
                }
                if (ordinal >= imports_count) {
                    return -1;
                }
                const resolved_import_t *import = &imports[ordinal];
                int64_t addend = (int64_t)(encoded_addend << 45) >> 45;
                if (!add_signed(import->address, import->addend, &fixed) ||
                    !add_signed(fixed, addend, &fixed)) {
                    return -1;
                }
                next = chain_next;
            }
        } else if (arm64e) {
            if (authenticated) {
                struct dyld_chained_ptr_arm64e_auth_rebase pointer = { 0 };
                memcpy(&pointer, &raw, sizeof(pointer));
                if (pointer.target > UINT64_MAX - remote_base) {
                    return -1;
                }
                fixed = remote_base + pointer.target;
                auth_key = pointer.key;
                auth_discriminator = pointer.addrDiv
                    ? remote_base + chain_offset
                    : pointer.diversity;
                next = pointer.next;
            } else {
                struct dyld_chained_ptr_arm64e_rebase pointer = { 0 };
                memcpy(&pointer, &raw, sizeof(pointer));
                uint64_t target = pointer.target |
                    ((uint64_t)pointer.high8 << 56);
                if (pointer_format == DYLD_CHAINED_PTR_ARM64E_USERLAND ||
                    pointer_format == DYLD_CHAINED_PTR_ARM64E_USERLAND24) {
                    if (target > UINT64_MAX - remote_base) {
                        return -1;
                    }
                    fixed = remote_base + target;
                } else {
                    if (target > UINT64_MAX - slide) {
                        return -1;
                    }
                    fixed = target + slide;
                }
                next = pointer.next;
            }
        } else if (bind) {
            struct dyld_chained_ptr_64_bind bind = { 0 };
            memcpy(&bind, &raw, sizeof(bind));
            if (bind.ordinal >= imports_count) {
                return -1;
            }

            const resolved_import_t *import = &imports[bind.ordinal];
            if (!add_signed(import->address, import->addend, &fixed) ||
                fixed > UINT64_MAX - bind.addend) {
                return -1;
            }
            fixed += bind.addend;
            next = bind.next;
        } else {
            struct dyld_chained_ptr_64_rebase rebase = { 0 };
            memcpy(&rebase, &raw, sizeof(rebase));
            uint64_t target = rebase.target | ((uint64_t)rebase.high8 << 56);
            if (pointer_format == DYLD_CHAINED_PTR_64_OFFSET) {
                if (target > UINT64_MAX - remote_base) {
                    return -1;
                }
                fixed = remote_base + target;
            } else {
                if (target > UINT64_MAX - slide) {
                    return -1;
                }
                fixed = target + slide;
            }
            next = rebase.next;
        }

        if (authenticated) {
            if (append_authenticated_fixup(authenticated_fixups,
                                           authenticated_fixup_count,
                                           remote_base + chain_offset,
                                           fixed,
                                           auth_key,
                                           auth_discriminator) != 0) {
                return -1;
            }
        } else if (remote_vm_write(task,
                                   remote_base + chain_offset,
                                   &fixed,
                                   sizeof(fixed)) != 0) {
                return -1;
        }

        if (next == 0) {
            return 0;
        }

        uint64_t stride = arm64e ? 8 : 4;
        uint64_t advance = (uint64_t)next * stride;
        if (chain_offset > UINT64_MAX - advance) {
            return -1;
        }
        chain_offset += advance;
    }
}

static int fix_page(mach_port_t task,
                    uint64_t remote_base,
                    uint64_t mapped_size,
                    const struct dyld_chained_starts_in_segment *starts,
                    uint16_t page_index,
                    uint16_t start,
                    const resolved_import_t *imports,
                    uint32_t imports_count,
                    uint64_t slide,
                    authenticated_fixup_t **authenticated_fixups,
                    size_t *authenticated_fixup_count)
{
    if (starts->page_size < sizeof(uint64_t) ||
        page_index > (UINT64_MAX - starts->segment_offset) /
            starts->page_size) {
        return -1;
    }

    uint64_t page_offset = starts->segment_offset +
        (uint64_t)page_index * starts->page_size;
    if (page_offset > mapped_size || starts->page_size > mapped_size - page_offset ||
        start > starts->page_size - sizeof(uint64_t)) {
        return -1;
    }

    return fix_pointer_chain(task,
                             remote_base,
                             mapped_size,
                             starts->pointer_format,
                             page_offset + start,
                             page_offset + starts->page_size,
                             imports,
                             imports_count,
                             slide,
                             authenticated_fixups,
                             authenticated_fixup_count);
}

static int fix_segment(mach_port_t task,
                       uint64_t remote_base,
                       uint64_t mapped_size,
                       const struct dyld_chained_starts_in_segment *starts,
                       const resolved_import_t *imports,
                       uint32_t imports_count,
                       uint64_t slide,
                       authenticated_fixup_t **authenticated_fixups,
                       size_t *authenticated_fixup_count)
{
    if (starts->pointer_format != DYLD_CHAINED_PTR_64 &&
        starts->pointer_format != DYLD_CHAINED_PTR_64_OFFSET &&
        starts->pointer_format != DYLD_CHAINED_PTR_ARM64E &&
        starts->pointer_format != DYLD_CHAINED_PTR_ARM64E_USERLAND &&
        starts->pointer_format != DYLD_CHAINED_PTR_ARM64E_USERLAND24) {
        return -1;
    }

    size_t page_array_size =
        (size_t)starts->page_count * sizeof(uint16_t);
    if (starts->size < offsetof(struct dyld_chained_starts_in_segment,
                                page_start) + page_array_size) {
        return -1;
    }

    size_t start_count =
        (starts->size - offsetof(struct dyld_chained_starts_in_segment,
                                 page_start)) / sizeof(uint16_t);

    for (uint16_t page = 0; page < starts->page_count; page++) {
        uint16_t entry = starts->page_start[page];
        if (entry == DYLD_CHAINED_PTR_START_NONE) {
            continue;
        }

        if ((entry & DYLD_CHAINED_PTR_START_MULTI) == 0) {
            if (fix_page(task,
                         remote_base,
                         mapped_size,
                         starts,
                         page,
                         entry,
                         imports,
                         imports_count,
                         slide,
                         authenticated_fixups,
                         authenticated_fixup_count) != 0) {
                return -1;
            }
            continue;
        }

        size_t chain_index = entry & ~DYLD_CHAINED_PTR_START_MULTI;
        for (;;) {
            if (chain_index >= start_count) {
                return -1;
            }
            uint16_t chain_start = starts->page_start[chain_index++];
            bool is_last = (chain_start & DYLD_CHAINED_PTR_START_LAST) != 0;
            chain_start &= ~DYLD_CHAINED_PTR_START_LAST;
            if (fix_page(task,
                         remote_base,
                         mapped_size,
                         starts,
                         page,
                         chain_start,
                         imports,
                         imports_count,
                         slide,
                         authenticated_fixups,
                         authenticated_fixup_count) != 0) {
                return -1;
            }
            if (is_last) {
                break;
            }
        }
    }

    return 0;
}

int apply_chained_fixups(mach_port_t task,
                         const mach_o_parser_context_t *parser,
                         const macho_image_layout_t *layout,
                         uint64_t remote_base,
                         uint64_t mapped_size)
{
    if (task == MACH_PORT_NULL || parser == NULL || layout == NULL ||
        remote_base == 0 || mapped_size < sizeof(uint64_t)) {
        return -1;
    }

    chained_fixups_t fixups = { 0 };
    int found = find_chained_fixups(parser, &fixups);
    if (found <= 0) {
        return found;
    }

    const struct dyld_chained_fixups_header *header = fixups.header;
    if (header->fixups_version != 0 || header->symbols_format != 0 ||
        !range_is_valid(header->starts_offset,
                        sizeof(struct dyld_chained_starts_in_image),
                        fixups.size)) {
        return -1;
    }

    resolved_import_t *imports = NULL;
    if (resolve_imports(task, &fixups, &imports) != 0) {
        return -1;
    }

    const struct dyld_chained_starts_in_image *starts =
        (const struct dyld_chained_starts_in_image *)(
            fixups.data + header->starts_offset);
    size_t offsets_size = (size_t)starts->seg_count * sizeof(uint32_t);
    if (!range_is_valid(header->starts_offset + sizeof(uint32_t),
                        offsets_size,
                        fixups.size)) {
        free(imports);
        return -1;
    }

    uint64_t slide = remote_base - layout->minimum_vm_address;
    authenticated_fixup_t *authenticated_fixups = NULL;
    size_t authenticated_fixup_count = 0;
    for (uint32_t index = 0; index < starts->seg_count; index++) {
        uint32_t segment_info_offset = starts->seg_info_offset[index];
        if (segment_info_offset == 0) {
            continue;
        }

        if (segment_info_offset > SIZE_MAX - header->starts_offset) {
            free(imports);
            return -1;
        }
        size_t offset = header->starts_offset + segment_info_offset;
        if (!range_is_valid(offset,
                            sizeof(struct dyld_chained_starts_in_segment),
                            fixups.size)) {
            free(imports);
            return -1;
        }

        const struct dyld_chained_starts_in_segment *segment_starts =
            (const struct dyld_chained_starts_in_segment *)(
                fixups.data + offset);
        if (!range_is_valid(offset, segment_starts->size, fixups.size) ||
            fix_segment(task,
                        remote_base,
                        mapped_size,
                        segment_starts,
                        imports,
                        header->imports_count,
                        slide,
                        &authenticated_fixups,
                        &authenticated_fixup_count) != 0) {
            free(authenticated_fixups);
            free(imports);
            return -1;
        }
    }

    if (authenticated_fixup_count != 0) {
        uint64_t *slots = calloc(authenticated_fixup_count, sizeof(*slots));
        uint64_t *values = calloc(authenticated_fixup_count, sizeof(*values));
        uint8_t *keys = calloc(authenticated_fixup_count, sizeof(*keys));
        uint64_t *discriminators = calloc(authenticated_fixup_count,
                                          sizeof(*discriminators));
        if (slots == NULL || values == NULL || keys == NULL ||
            discriminators == NULL) {
            free(slots); free(values); free(keys); free(discriminators);
            free(authenticated_fixups);
            free(imports);
            return -1;
        }
        for (size_t index = 0; index < authenticated_fixup_count; index++) {
            slots[index] = authenticated_fixups[index].slot_address;
            values[index] = authenticated_fixups[index].value;
            keys[index] = authenticated_fixups[index].key;
            discriminators[index] = authenticated_fixups[index].discriminator;
        }
        int sign_result = remote_vm_sign_and_write_pointers(
            task,
            slots,
            values,
            keys,
            discriminators,
            authenticated_fixup_count
        );
        free(slots); free(values); free(keys); free(discriminators);
        if (sign_result != 0) {
            free(authenticated_fixups);
            free(imports);
            return -1;
        }
    }

    free(authenticated_fixups);
    free(imports);
    return 1;
}
