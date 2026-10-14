#include "setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static tweak_config_store_t configured_tweaks;
static char configured_os_version[64];

static bool debug_mode_enabled(void)
{
    const char *value = getenv("DEBUG_MODE");
    return value != NULL && strcmp(value, "true") == 0;
}

bool setup(void)
{
    size_t version_size = sizeof(configured_os_version);
    if (sysctlbyname(
            "kern.osproductversion",
            configured_os_version,
            &version_size,
            NULL,
            0
        ) != 0) {
        strlcpy(configured_os_version, "*", sizeof(configured_os_version));
    }

    if (getppid() != 1 && !debug_mode_enabled()) {
        fprintf(
            stderr,
            "Squid was not started by launchd. "
            "Set DEBUG_MODE=\"true\" for development.\n"
        );
        return false;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Squid must run as root.\n");
        return false;
    }

    if (!tweak_config_store_load(&configured_tweaks)) {
        return false;
    }

    printf("Loaded %zu Squid tweak configuration(s) for macOS %s.\n",
           configured_tweaks.count,
           configured_os_version);
    return true;
}

bool reload_tweak_configs(void)
{
    tweak_config_store_t refreshed = { 0 };
    if (!tweak_config_store_load(&refreshed)) {
        return false;
    }

    tweak_config_store_destroy(&configured_tweaks);
    configured_tweaks = refreshed;
    return true;
}

const tweak_config_store_t *tweak_configs(void)
{
    return &configured_tweaks;
}

const char *current_os_version(void)
{
    return configured_os_version;
}
