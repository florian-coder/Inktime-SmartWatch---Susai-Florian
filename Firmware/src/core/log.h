/* Line-oriented debug log. Goes to UARTE0 on the watch and to stdout in the simulator.
 * Tests (host and Renode) match on these lines, so keep the "tag: message" format stable. */
#ifndef LOG_H
#define LOG_H

#if defined(__GNUC__)
#define LOG_PRINTF_ATTR __attribute__((format(printf, 1, 2)))
#else
#define LOG_PRINTF_ATTR
#endif

void log_printf(const char *fmt, ...) LOG_PRINTF_ATTR;

#endif
