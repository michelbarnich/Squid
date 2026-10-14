#ifndef RELOCATION_H
#define RELOCATION_H

#include "mach_o_parser.h"

#include <mach/mach.h>
#include <stddef.h>
#include <stdint.h>

// Applies LC_DYLD_CHAINED_FIXUPS to an image already copied into a task.
// Imported symbols are resolved from the injector's loaded images. Returns 1
// when applied, 0 when absent, and -1 on failure.
int apply_chained_fixups(mach_port_t task,
                         const mach_o_parser_context_t *parser,
                         const macho_image_layout_t *layout,
                         uint64_t remote_base,
                         uint64_t mapped_size);

#endif // RELOCATION_H
