#ifndef MACH_O_PARSER_H
#define MACH_O_PARSER_H

#include <mach-o/loader.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t len;
    struct mach_header_64 *header;
    int valid;
} mach_o_parser_context_t;

typedef struct {
    uint64_t vmsize;
    uint64_t vmaddr;
    uint64_t fileoff;
    uint32_t flags;
    uint32_t nsects;
} macho_segment_info_t;

typedef struct {
    uint64_t minimum_vm_address;
    uint64_t maximum_vm_address;
    uint64_t mapped_size;
} macho_image_layout_t;

int macho_validate(const uint8_t *data, size_t len, mach_o_parser_context_t *ctx);
int macho_get_segments(mach_o_parser_context_t *ctx,
                       macho_segment_info_t *segments,
                       size_t max_segments);
int macho_calculate_memory_layout(mach_o_parser_context_t *ctx,
                                  uint64_t *total_size,
                                  uint64_t *max_vaddr);
int macho_get_image_layout(mach_o_parser_context_t *ctx,
                           macho_image_layout_t *layout);
int macho_resolve_symbol(mach_o_parser_context_t *ctx,
                         const char *symbol_name,
                         uint64_t slide,
                         uint64_t *address);
int macho_requires_dynamic_fixups(mach_o_parser_context_t *ctx);

#endif /* MACH_O_PARSER_H */
