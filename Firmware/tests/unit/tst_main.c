/*
 * Test runner. Usage: unit_tests [-v] [--junit report.xml] [name-filter]
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tst.h"

#define MAX_TESTS 512
#define MAX_MSG   2048

typedef struct {
    const char *name;
    const char *file;
    tst_fn fn;
    int failures;
    double seconds;
    char msg[MAX_MSG];
} test_t;

static test_t s_tests[MAX_TESTS];
static int s_count;
static test_t *s_current;
jmp_buf tst_abort;

void tst_register(const char *name, const char *file, tst_fn fn)
{
    if (s_count < MAX_TESTS) {
        s_tests[s_count++] = (test_t){ .name = name, .file = file, .fn = fn };
    }
}

void tst_fail(const char *file, int line, const char *fmt, ...)
{
    test_t *t = s_current;
    t->failures++;
    size_t used = strlen(t->msg);
    if (used < MAX_MSG - 1) {
        int n = snprintf(t->msg + used, MAX_MSG - used, "%s:%d: ", file, line);
        used += (size_t)(n > 0 ? n : 0);
        if (used < MAX_MSG - 1) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(t->msg + used, MAX_MSG - used, fmt, ap);
            va_end(ap);
            used = strlen(t->msg);
            if (used < MAX_MSG - 1) {
                strcat(t->msg, "\n");
            }
        }
    }
}

static int by_file_then_name(const void *a, const void *b)
{
    const test_t *x = a, *y = b;
    int c = strcmp(x->file, y->file);
    return c ? c : strcmp(x->name, y->name);
}

/* setjmp lives in its own frame so main()'s locals cannot be clobbered by longjmp. */
static void run_one(test_t *t)
{
    s_current = t;
    clock_t c0 = clock();
    if (setjmp(tst_abort) == 0) {
        t->fn();
    }
    t->seconds = (double)(clock() - c0) / CLOCKS_PER_SEC;
}

static void xml_escape(FILE *f, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '&': fputs("&amp;", f); break;
        case '"': fputs("&quot;", f); break;
        default: fputc(*s, f); break;
        }
    }
}

int main(int argc, char **argv)
{
    const char *filter = NULL;
    const char *junit = NULL;
    int verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) {
            verbose = 1;
        } else if (!strcmp(argv[i], "--junit") && i + 1 < argc) {
            junit = argv[++i];
        } else {
            filter = argv[i];
        }
    }

    qsort(s_tests, (size_t)s_count, sizeof(s_tests[0]), by_file_then_name);
    int run = 0, failed = 0;
    for (int i = 0; i < s_count; i++) {
        test_t *t = &s_tests[i];
        if (filter && !strstr(t->name, filter) && !strstr(t->file, filter)) {
            continue;
        }
        run_one(t);
        run++;
        if (t->failures) {
            failed++;
            printf("FAIL %s\n%s", t->name, t->msg);
        } else if (verbose) {
            printf("ok   %s (%.3fs)\n", t->name, t->seconds);
        }
    }
    printf("\n%d tests, %d passed, %d failed\n", run, run - failed, failed);

    if (junit) {
        FILE *f = fopen(junit, "w");
        if (f) {
            fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
            fprintf(f, "<testsuite name=\"inktime-unit\" tests=\"%d\" failures=\"%d\">\n", run, failed);
            for (int i = 0; i < s_count; i++) {
                test_t *t = &s_tests[i];
                if (filter && !strstr(t->name, filter) && !strstr(t->file, filter)) {
                    continue;
                }
                fprintf(f, "  <testcase classname=\"%s\" name=\"%s\" time=\"%.4f\"", t->file, t->name, t->seconds);
                if (t->failures) {
                    fprintf(f, ">\n    <failure message=\"%d check(s) failed\">", t->failures);
                    xml_escape(f, t->msg);
                    fprintf(f, "</failure>\n  </testcase>\n");
                } else {
                    fprintf(f, "/>\n");
                }
            }
            fprintf(f, "</testsuite>\n");
            fclose(f);
        }
    }
    return failed ? 1 : 0;
}
