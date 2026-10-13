#include "scenario.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app.h"
#include "core/datetime.h"
#include "models/models.h"
#include "pbm.h"
#include "services/display.h"
#include "sim.h"
#include "watch.h"

#define MAX_ARGS 8

static void report(scn_ctx_t *c, const char *kind, const char *a, const char *b)
{
    fprintf(stderr, "%s:%d: %s: %s %s\n", c->file ? c->file : "<stdin>", c->line, kind, a, b);
}

static int fail(scn_ctx_t *c, const char *what, const char *detail)
{
    c->failures++;
    report(c, "FAILED", what, detail);
    return 1;
}

static int syntax(scn_ctx_t *c, const char *what)
{
    report(c, "syntax error", what, "");
    return -1;
}

static bool parse_button(const char *s, btn_id_t *b)
{
    if (!strcmp(s, "up"))
        *b = BTN_UP;
    else if (!strcmp(s, "enter"))
        *b = BTN_ENTER;
    else if (!strcmp(s, "down"))
        *b = BTN_DOWN;
    else
        return false;
    return true;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*s == '\0' || *end != '\0') {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static void join_path(char *out, size_t n, const char *dir, const char *file)
{
    if (dir && file[0] != '/') {
        snprintf(out, n, "%s/%s", dir, file);
    } else {
        snprintf(out, n, "%s", file);
    }
}

static int expect_panel(scn_ctx_t *c, const char *name)
{
    static gfx_t panel, golden, diff;
    char path[512];
    sim_panel(&panel);
    join_path(path, sizeof(path), c->golden_dir, name);
    if (c->update_golden) {
        if (!pbm_write(path, &panel)) {
            return fail(c, "cannot write golden", path);
        }
        printf("updated %s\n", path);
        return 0;
    }
    if (!pbm_read(path, &golden)) {
        return fail(c, "missing golden image (run `make golden`):", path);
    }
    int n = pbm_compare(&panel, &golden, &diff);
    if (n != 0) {
        char base[256], actual[512], dpath[512], msg[1100];
        snprintf(base, sizeof(base), "%s", name);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        char actual_name[300], diff_name[300];
        snprintf(actual_name, sizeof(actual_name), "%s.actual.pbm", base);
        snprintf(diff_name, sizeof(diff_name), "%s.diff.pbm", base);
        join_path(actual, sizeof(actual), c->out_dir, actual_name);
        join_path(dpath, sizeof(dpath), c->out_dir, diff_name);
        pbm_write(actual, &panel);
        pbm_write(dpath, &diff);
        snprintf(msg, sizeof(msg), "%d pixels differ (see %s, %s)", n, actual, dpath);
        return fail(c, "panel != golden:", msg);
    }
    return 0;
}

static int cmd_expect(scn_ctx_t *c, int argc, char **argv)
{
    if (argc < 2) {
        return syntax(c, "expect what?");
    }
    const char *what = argv[1];
    uint32_t n = 0;
    char buf[160];

    if (!strcmp(what, "screen") && argc == 3) {
        const char *actual = app_screen_name(watch_app()->screen);
        return strcmp(actual, argv[2]) ? fail(c, "screen is", actual) : 0;
    }
    if (!strcmp(what, "panel") && argc == 3) {
        return expect_panel(c, argv[2]);
    }
    if (!strcmp(what, "haptic") && argc == 3 && parse_u32(argv[2], &n)) {
        snprintf(buf, sizeof(buf), "%u", hapm_last_effect());
        return hapm_last_effect() != n ? fail(c, "last haptic effect is", buf) : 0;
    }
    if (!strcmp(what, "full") && argc == 3 && parse_u32(argv[2], &n)) {
        snprintf(buf, sizeof(buf), "%" PRIu32, epdm_stats()->full);
        return epdm_stats()->full != n ? fail(c, "full refresh count is", buf) : 0;
    }
    if (!strcmp(what, "partial") && argc == 3 && parse_u32(argv[2], &n)) {
        snprintf(buf, sizeof(buf), "%" PRIu32, epdm_stats()->partial);
        return epdm_stats()->partial != n ? fail(c, "partial refresh count is", buf) : 0;
    }
    if (!strcmp(what, "steps") && argc == 3 && parse_u32(argv[2], &n)) {
        snprintf(buf, sizeof(buf), "%" PRIu32, app_steps_today(watch_app()));
        return app_steps_today(watch_app()) != n ? fail(c, "steps today is", buf) : 0;
    }
    if (!strcmp(what, "off") && argc == 2) {
        return sim_is_off() ? 0 : fail(c, "watch is", "on");
    }
    if (!strcmp(what, "on") && argc == 2) {
        return sim_is_off() ? fail(c, "watch is", "off") : 0;
    }
    if (!strcmp(what, "noerrors") && argc == 2) {
        return sim_model_errors() ? fail(c, "peripheral model error:", sim_model_error_text()) : 0;
    }
    if (!strcmp(what, "log") && argc >= 3) {
        buf[0] = '\0';
        for (int i = 2; i < argc; i++) {
            strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 2);
            if (i + 1 < argc) strcat(buf, " ");
        }
        int idx = sim_log_find(buf, c->log_cursor);
        if (idx < 0) {
            return fail(c, "log line not found:", buf);
        }
        c->log_cursor = idx + 1;
        return 0;
    }
    return syntax(c, "unknown expectation");
}

static void status(void)
{
    const app_t *a = watch_app();
    datetime_t dt;
    dt_from_epoch(app_epoch(a, sim_now()), &dt);
    printf("t=%.3fs  %04u-%02u-%02u %02u:%02u:%02u  screen=%s  battery=%u%%%s  steps=%" PRIu32 "  refresh full=%" PRIu32
           " partial=%" PRIu32 "  haptics=%" PRIu32 "%s\n",
           (double)sim_now() / 1000.0, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
           app_screen_name(a->screen), a->power.pct, a->power.charging ? " (charging)" : "", app_steps_today(a),
           epdm_stats()->full, epdm_stats()->partial, hapm_play_count(), sim_is_off() ? "  [SYSTEM OFF]" : "");
}

int scenario_exec(scn_ctx_t *c, const char *line_in)
{
    char line[256];
    snprintf(line, sizeof(line), "%s", line_in);
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';

    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *tok = strtok(line, " \t\r\n"); tok && argc < MAX_ARGS; tok = strtok(NULL, " \t\r\n")) {
        argv[argc++] = tok;
    }
    if (argc == 0) {
        return 0;
    }
    const char *cmd = argv[0];
    uint32_t n = 0, m = 0;
    btn_id_t b;

    if (!strcmp(cmd, "boot")) {
        sim_init();
        c->log_cursor = 0;
        return 0;
    }
    if ((!strcmp(cmd, "wait") || !strcmp(cmd, "run")) && argc == 2 && parse_u32(argv[1], &n)) {
        return sim_run_for(n) ? 0 : fail(c, "firmware", "livelocked");
    }
    if (!strcmp(cmd, "settle")) {
        n = 10000;
        if (argc == 2 && !parse_u32(argv[1], &n)) return syntax(c, "settle [max_ms]");
        return sim_settle(n) ? 0 : fail(c, "display did not settle", "");
    }
    if (!strcmp(cmd, "press") && (argc == 2 || argc == 3) && parse_button(argv[1], &b)) {
        n = 100;
        if (argc == 3 && !parse_u32(argv[2], &n)) return syntax(c, "press <button> [ms]");
        sim_click(b, n);
        return 0;
    }
    if (!strcmp(cmd, "hold") && argc == 3 && parse_button(argv[1], &b) && parse_u32(argv[2], &n)) {
        sim_click(b, n);
        return 0;
    }
    if (!strcmp(cmd, "steps") && argc == 2 && parse_u32(argv[1], &n)) {
        sim_add_steps(n);
        return 0;
    }
    if (!strcmp(cmd, "battery") && argc == 3 && parse_u32(argv[1], &n) && parse_u32(argv[2], &m)) {
        sim_set_battery((uint8_t)n, (uint16_t)m);
        return 0;
    }
    if (!strcmp(cmd, "charger") && argc == 2) {
        if (!strcmp(argv[1], "off"))
            sim_set_charger(false, CHG_NOT_CHARGING);
        else if (!strcmp(argv[1], "cc"))
            sim_set_charger(true, CHG_CONSTANT_CURRENT);
        else if (!strcmp(argv[1], "cv"))
            sim_set_charger(true, CHG_CONSTANT_VOLTAGE);
        else if (!strcmp(argv[1], "done"))
            sim_set_charger(true, CHG_DONE);
        else
            return syntax(c, "charger <off|cc|cv|done>");
        return 0;
    }
    if (!strcmp(cmd, "notify") && argc >= 2) {
        const char *text = line_in + (argv[1] - line);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s", text);
        msg[strcspn(msg, "#\r\n")] = '\0';
        sim_notify(msg);
        return 0;
    }
    if (!strcmp(cmd, "time") && argc == 3) {
        datetime_t dt = { 0 };
        unsigned y, mo, d, h, mi, s;
        if (sscanf(argv[1], "%u-%u-%u", &y, &mo, &d) != 3 || sscanf(argv[2], "%u:%u:%u", &h, &mi, &s) != 3) {
            return syntax(c, "time YYYY-MM-DD HH:MM:SS");
        }
        dt = (datetime_t){ .year = (uint16_t)y,
                           .month = (uint8_t)mo,
                           .day = (uint8_t)d,
                           .hour = (uint8_t)h,
                           .minute = (uint8_t)mi,
                           .second = (uint8_t)s };
        sim_set_epoch(dt_to_epoch(&dt));
        return 0;
    }
    if (!strcmp(cmd, "expect")) {
        return cmd_expect(c, argc, argv);
    }
    if (!strcmp(cmd, "dump") && argc == 2) {
        static gfx_t panel;
        char path[512];
        sim_panel(&panel);
        join_path(path, sizeof(path), c->out_dir, argv[1]);
        if (!pbm_write(path, &panel)) return fail(c, "cannot write", path);
        printf("wrote %s\n", path);
        return 0;
    }
    if (!strcmp(cmd, "ascii")) {
        static gfx_t panel;
        sim_panel(&panel);
        pbm_print_ascii(&panel);
        return 0;
    }
    if (!strcmp(cmd, "status")) {
        status();
        return 0;
    }
    if (!strcmp(cmd, "log") && argc == 2) {
        sim_log_echo(!strcmp(argv[1], "on"));
        return 0;
    }
    if (!strcmp(cmd, "echo")) {
        for (int i = 1; i < argc; i++)
            printf("%s%s", argv[i], i + 1 < argc ? " " : "\n");
        return 0;
    }
    if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
        c->quit = true;
        return 0;
    }
    if (!strcmp(cmd, "help")) {
        printf("commands: boot, wait <ms>, settle, press <up|enter|down> [ms], hold <btn> <ms>,\n"
               "          time <YYYY-MM-DD> <HH:MM:SS>, steps <n>, battery <pct> <mV>,\n"
               "          charger <off|cc|cv|done>, notify <text>, ascii, status, dump <f.pbm>,\n"
               "          log <on|off>, expect ..., quit\n");
        return 0;
    }
    return syntax(c, cmd);
}

int scenario_run_file(scn_ctx_t *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    char line[256];
    c->file = path;
    c->line = 0;
    int rc = 0;
    while (fgets(line, sizeof(line), f) && !c->quit) {
        c->line++;
        int r = scenario_exec(c, line);
        if (r < 0) {
            rc = -1;
            break;
        }
        if (r > 0) {
            rc = 1;
        }
    }
    fclose(f);
    return rc;
}
