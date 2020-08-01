#ifndef __LOG_H__
#define __LOG_H__

#include <stdarg.h>

#define LOG_LEVEL_INFO 0
#define LOG_LEVEL_DEBUG 1

static int _log_level = LOG_LEVEL_DEBUG;

void _addon_log(int level, const char *fmt, ...);

#define addon_log(level, fmt, ...) _addon_log(level, fmt"\n", ##__VA_ARGS__)

#endif
