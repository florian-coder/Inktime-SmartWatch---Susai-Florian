/*
 * inktime_sim: run the InkTime firmware on the host.
 *
 *   inktime_sim                         interactive (SDL window if built with SDL, else REPL)
 *   inktime_sim --repl                  text REPL (see `help`)
 *   inktime_sim --scenario f.scn ...    scripted run; exit code 1 on failed expectations
 *     [--golden DIR] [--out DIR] [--update-golden] [--log]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scenario.h"
#include "sim.h"

#ifdef HAVE_SDL
int sim_gui_run(int scale, double speed, const char *shot);
#endif

static int repl(void)
{
    scn_ctx_t c = { .out_dir = ".", .golden_dir = "." };
    char line[256];
    sim_log_echo(true);
    sim_init();
    printf("InkTime simulator - type `help`. Buttons: press up|enter|down, `ascii` shows the panel.\n");
    while (!c.quit) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        c.line++;
        scenario_exec(&c, line);
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "usage: inktime_sim [--repl | --gui [--scale N] [--speed X] |\n"
                    "                    --scenario FILE... [--golden DIR] [--out DIR] [--update-golden] [--log]]\n");
}

int main(int argc, char **argv)
{
    const char *scenarios[64];
    int n_scen = 0;
    scn_ctx_t c = { .golden_dir = "tests/golden", .out_dir = "." };
    int mode_repl = 0, mode_gui = 0, scale = 3;
    double speed = 1.0;
    const char *shot = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scenario") && i + 1 < argc) {
            while (i + 1 < argc && argv[i + 1][0] != '-' && n_scen < 64) {
                scenarios[n_scen++] = argv[++i];
            }
        } else if (!strcmp(argv[i], "--golden") && i + 1 < argc) {
            c.golden_dir = argv[++i];
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            c.out_dir = argv[++i];
        } else if (!strcmp(argv[i], "--update-golden")) {
            c.update_golden = true;
        } else if (!strcmp(argv[i], "--log")) {
            sim_log_echo(true);
        } else if (!strcmp(argv[i], "--repl")) {
            mode_repl = 1;
        } else if (!strcmp(argv[i], "--gui")) {
            mode_gui = 1;
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--speed") && i + 1 < argc) {
            speed = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--gui-shot") && i + 1 < argc) {
            shot = argv[++i];
            mode_gui = 1;
        } else {
            usage();
            return 2;
        }
    }

    if (n_scen > 0) {
        int failed = 0;
        for (int i = 0; i < n_scen; i++) {
            c.failures = 0;
            c.quit = false;
            int rc = scenario_run_file(&c, scenarios[i]);
            printf("%-40s %s\n", scenarios[i], rc == 0 ? "PASS" : rc < 0 ? "ERROR" : "FAIL");
            failed += rc != 0;
        }
        return failed ? 1 : 0;
    }

#ifdef HAVE_SDL
    if (!mode_repl && (mode_gui || getenv("DISPLAY") || getenv("WAYLAND_DISPLAY"))) {
        return sim_gui_run(scale, speed, shot);
    }
#else
    (void)scale;
    (void)speed;
    (void)mode_repl;
    (void)shot;
    if (mode_gui) {
        fprintf(stderr, "built without SDL2: install libsdl2-dev and rebuild for the window\n");
    }
#endif
    return repl();
}
