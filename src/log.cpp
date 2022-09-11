#include "log.hpp"
#include <stdio.h>

void _addon_log(int level, const char *fmt, ...) {
    va_list args;

    /* Check if the message should be logged */
    if (level > _log_level)
        return;

    va_start(args, fmt);
    flockfile(stdout);
    vprintf(fmt, args);
    funlockfile(stdout);
    va_end(args);
}


