/*
 * Scenario language for scripted end-to-end runs (also the REPL syntax).
 *
 *   boot                          fresh board, firmware boots
 *   wait <ms> | settle [max_ms]   advance virtual time
 *   press <up|enter|down> [ms]    click (default hold 100 ms)
 *   hold <button> <ms>            long press
 *   time <YYYY-MM-DD> <HH:MM:SS>  set the wall clock
 *   steps <n> | battery <pct> <mV> | charger <off|cc|cv|done> | notify <text>
 *   expect screen <name> | panel <golden.pbm> | haptic <effect> | log <text>
 *   expect full <n> | partial <n> | steps <n> | off | on | noerrors
 *   dump <file.pbm> | ascii | status | log <on|off> | echo <text>
 */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdbool.h>

typedef struct {
    const char *golden_dir;
    const char *out_dir;
    const char *file;
    int line;
    bool update_golden;
    int failures;
    int log_cursor;
    bool quit;
} scn_ctx_t;

/* Returns 0 on success, 1 if an expectation failed, -1 on a syntax error. */
int scenario_exec(scn_ctx_t *c, const char *line);
int scenario_run_file(scn_ctx_t *c, const char *path);

#endif
