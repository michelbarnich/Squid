//
//  code_identity.h
//  Squid
//

#ifndef CODE_IDENTITY_H
#define CODE_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

bool copy_process_signing_identifier(
    pid_t pid,
    char *buffer,
    size_t buffer_size
);

#endif
