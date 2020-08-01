#include "log.hpp"
#include <stdio.h>

void _addon_log(int level, const char *fmt, ...) {
    va_list arg;

    /* Check if the message should be logged */
    if (level > _log_level)
        return;

    va_start(arg, fmt);
    printf(fmt, arg);
    va_end(arg);
}


