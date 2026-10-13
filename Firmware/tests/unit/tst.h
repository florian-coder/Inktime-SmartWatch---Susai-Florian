/*
 * Minimal unit test framework: TEST() self-registers through a constructor,
 * CHECK*() records failures and continues, REQUIRE() aborts the current test.
 */
#ifndef TST_H
#define TST_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef void (*tst_fn)(void);

void tst_register(const char *name, const char *file, tst_fn fn);
void tst_fail(const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
extern jmp_buf tst_abort;

#define TEST(name)                                                                                                     \
    static void name(void);                                                                                            \
    __attribute__((constructor)) static void tst_reg_##name(void)                                                      \
    {                                                                                                                  \
        tst_register(#name, __FILE__, name);                                                                           \
    }                                                                                                                  \
    static void name(void)

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) tst_fail(__FILE__, __LINE__, "CHECK(%s)", #cond);                                                 \
    } while (0)

#define REQUIRE(cond)                                                                                                  \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            tst_fail(__FILE__, __LINE__, "REQUIRE(%s)", #cond);                                                        \
            longjmp(tst_abort, 1);                                                                                     \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        long long tst_a = (long long)(a), tst_b = (long long)(b);                                                      \
        if (tst_a != tst_b) tst_fail(__FILE__, __LINE__, "CHECK_EQ(%s, %s): %lld != %lld", #a, #b, tst_a, tst_b);      \
    } while (0)

#define CHECK_STR(a, b)                                                                                                \
    do {                                                                                                               \
        const char *tst_a = (a), *tst_b = (b);                                                                         \
        if (strcmp(tst_a, tst_b) != 0)                                                                                 \
            tst_fail(__FILE__, __LINE__, "CHECK_STR(%s, %s): \"%s\" != \"%s\"", #a, #b, tst_a, tst_b);                 \
    } while (0)

#endif
