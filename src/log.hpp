#ifndef __LOG_H__
#define __LOG_H__

#include <stdarg.h>

#define LL_INFO 0
#define L_DBG 1

static int _log_level = L_DBG;

void _addon_log(int level, const char *fmt, ...);

#define addon_log(level, fmt, ...) _addon_log(level, fmt"\n", ##__VA_ARGS__)

#endif
