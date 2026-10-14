//
//  main.c
//  Squid
//

#include "./daemon/daemon.h"
#include "./setup/setup.h"

#include <stdlib.h>

int main(void)
{
    if (!setup()) {
        return EXIT_FAILURE;
    }

    run_loop();
    return EXIT_SUCCESS;
}
