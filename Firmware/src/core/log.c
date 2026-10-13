#include "log.h"

#include <stdarg.h>
#include <stdio.h>

#include "hal/hal.h"

void log_printf(const char *fmt, ...)
{
    char line[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > sizeof(line) - 3) {
        n = (int)sizeof(line) - 3;
    }
    line[n++] = '\r';
    line[n++] = '\n';
    hal_log_write(line, (size_t)n);
}
