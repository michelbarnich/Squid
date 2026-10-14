//
//  setup.h
//  Squid
//
//  Created by Michel Barnich on 02.10.2026.
//

#ifndef SETUP_H
#define SETUP_H

#include <stdbool.h>

#include "tweak_config.h"

bool setup(void);
bool reload_tweak_configs(void);
const tweak_config_store_t *tweak_configs(void);
const char *current_os_version(void);

#endif
