#ifndef UTIL_H
#define UTIL_H

#define ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)        ((a) < (b) ? (a) : (b))
#define MAX(a, b)        ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* Earliest non-zero deadline (0 means "no deadline"). */
#define DEADLINE_MIN(a, b) ((a) == 0 ? (b) : ((b) == 0 ? (a) : MIN((a), (b))))

#endif
