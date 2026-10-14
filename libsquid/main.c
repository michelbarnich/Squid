//
//  main.c
//  Squid
//
//  Created by Michel Barnich on 03.10.2026.
//

#include <syslog.h>

__attribute__((visibility("default")))
volatile unsigned long long squid_tweak_initialized = 0;

__attribute__((constructor, visibility("default"), noinline))
static void squid_tweak_init(void)
{
    syslog(LOG_NOTICE,
           "[com.squid.tweak:injection] Squid initialized");
}
