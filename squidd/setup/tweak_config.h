#ifndef TWEAK_CONFIG_H
#define TWEAK_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define SQUID_TWEAK_IDENTIFIER_LENGTH 128
#define SQUID_TWEAK_BUNDLE_ID_LENGTH 256
#define SQUID_TWEAK_OS_VERSION_LENGTH 64
#define SQUID_TWEAK_LIBRARY_PATH_LENGTH 1024

typedef struct {
    char identifier[SQUID_TWEAK_IDENTIFIER_LENGTH];
    char target_bundle_id[SQUID_TWEAK_BUNDLE_ID_LENGTH];
    char os_version[SQUID_TWEAK_OS_VERSION_LENGTH];
    char library_path[SQUID_TWEAK_LIBRARY_PATH_LENGTH];
    bool enabled;
} tweak_config_t;

typedef struct {
    tweak_config_t *tweaks;
    size_t count;
} tweak_config_store_t;

bool tweak_config_store_load(tweak_config_store_t *store);
void tweak_config_store_destroy(tweak_config_store_t *store);
bool tweak_config_matches(
    const tweak_config_t *tweak,
    const char *bundle_id,
    const char *os_version
);

#endif
