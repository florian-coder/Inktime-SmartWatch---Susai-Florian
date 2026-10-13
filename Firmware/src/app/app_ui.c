/* Screen rendering. Layout is for the 200x200 panel; text scale 2 = 12x16 px cells. */
#include <stdio.h>
#include <string.h>

#include "app.h"

static void title(gfx_t *g, const char *text)
{
    gfx_fill(g, 0, 0, GFX_W, 28, true);
    gfx_text_center(g, 7, text, 2, false);
}

static void hint(gfx_t *g, const char *text)
{
    gfx_text_center(g, GFX_H - 12, text, 1, true);
}

static void seg_time(gfx_t *g, int x, int y, int w, int h, int t, int hh, int mm)
{
    const int gap = w / 6;
    const int colon = t + 2;
    gfx_seg_digit(g, x, y, w, h, t, hh / 10);
    x += w + gap;
    gfx_seg_digit(g, x, y, w, h, t, hh % 10);
    x += w + gap;
    gfx_fill(g, x, y + h / 3 - colon / 2, colon, colon, true);
    gfx_fill(g, x, y + 2 * h / 3 - colon / 2, colon, colon, true);
    x += colon + gap;
    gfx_seg_digit(g, x, y, w, h, t, mm / 10);
    x += w + gap;
    gfx_seg_digit(g, x, y, w, h, t, mm % 10);
}

static void watchface(const app_t *a, gfx_t *g, uint64_t now)
{
    char buf[32];
    datetime_t dt;
    dt_from_epoch(app_epoch(a, now), &dt);

    snprintf(buf, sizeof(buf), "%s %02u %s", dt_wday_name(dt.wday), dt.day, dt_month_name(dt.month));
    gfx_text(g, 6, 7, buf, 2, true);

    gfx_battery(g, 168, 8, 28, 14, a->power.pct);
    if (a->power.charging) {
        gfx_icon(g, 148, 7, ICON_BATTERY_BOLT);
    } else {
        snprintf(buf, sizeof(buf), "%u%%", a->power.pct);
        gfx_text_right(g, 163, 12, buf, 1, true);
    }
    gfx_fill(g, 0, 30, GFX_W, 2, true);

    seg_time(g, 7, 44, 38, 84, 9, dt.hour, dt.minute);

    gfx_fill(g, 0, 140, GFX_W, 2, true);
    gfx_icon(g, 6, 150, ICON_STEPS);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)a->wf_steps);
    gfx_text(g, 28, 151, buf, 2, true);
    if (a->alarm_on) {
        gfx_icon(g, 116, 150, ICON_BELL);
        snprintf(buf, sizeof(buf), "%02u:%02u", a->alarm_h, a->alarm_m);
        gfx_text(g, 136, 151, buf, 2, true);
    }
    gfx_progress(g, 6, 178, 188, 14, (int)(a->wf_steps * 1000u / APP_STEP_GOAL));
}

static void list_screen(gfx_t *g, const char *name, const char *const *items, int count, int sel)
{
    title(g, name);
    for (int i = 0; i < count; i++) {
        int y = 34 + i * 23;
        gfx_text(g, 12, y + 4, items[i], 2, true);
        if (i == sel) {
            gfx_invert(g, 4, y, GFX_W - 8, 22);
        }
    }
}

static void menu(const app_t *a, gfx_t *g)
{
    static const char *const items[MENU_COUNT] = {
        "STEPS", "STOPWATCH", "ALARM", "SET TIME", "BATTERY", "SETTINGS", "ABOUT",
    };
    list_screen(g, "MENU", items, MENU_COUNT, a->menu_idx);
}

static void settings(const app_t *a, gfx_t *g)
{
    char vib[20], full[20];
    snprintf(vib, sizeof(vib), "VIBRATE: %s", a->vibrate ? "ON" : "OFF");
    snprintf(full, sizeof(full), "FULL REF: %u", a->full_every);
    const char *items[SET_COUNT] = { vib, full, "POWER OFF" };
    list_screen(g, "SETTINGS", items, SET_COUNT, a->settings_idx);
}

static void steps(const app_t *a, gfx_t *g)
{
    char buf[32];
    uint32_t s = app_steps_today(a);
    uint32_t m = s * APP_STEP_LENGTH_MM / 1000;
    title(g, "STEPS");
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s);
    gfx_text_center(g, 44, buf, 4, true);
    gfx_text_center(g, 80, "STEPS TODAY", 1, true);
    snprintf(buf, sizeof(buf), "%lu.%02lu KM", (unsigned long)(m / 1000), (unsigned long)(m % 1000 / 10));
    gfx_text_center(g, 100, buf, 2, true);
    gfx_progress(g, 16, 130, 168, 16, (int)(s >= APP_STEP_GOAL ? 1000 : s * 1000u / APP_STEP_GOAL));
    snprintf(buf, sizeof(buf), "GOAL %lu", (unsigned long)APP_STEP_GOAL);
    gfx_text_center(g, 154, buf, 1, true);
    hint(g, "ENTER: BACK");
}

static void battery(const app_t *a, gfx_t *g)
{
    char buf[32];
    title(g, "BATTERY");
    snprintf(buf, sizeof(buf), "%u%%", a->power.pct);
    gfx_text_center(g, 42, buf, 4, true);
    snprintf(buf, sizeof(buf), "%u.%02u V", (unsigned)(a->power.mv / 1000), (unsigned)(a->power.mv % 1000 / 10));
    gfx_text_center(g, 82, buf, 2, true);
    const char *state = a->power.full ? "FULL" : a->power.charging ? "CHARGING" : a->power.vin ? "USB" : "ON BATTERY";
    gfx_text_center(g, 106, state, 2, true);
    gfx_battery(g, 40, 134, 120, 36, a->power.pct);
    hint(g, "ENTER: BACK");
}

static void stopwatch(const app_t *a, gfx_t *g, uint64_t now)
{
    char buf[32];
    uint64_t ms = a->sw_accum_ms + (a->sw_running ? now - a->sw_start_ms : 0);
    unsigned long sec = (unsigned long)(ms / 1000);
    title(g, "STOPWATCH");
    if (sec >= 3600) {
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", sec / 3600, sec / 60 % 60, sec % 60);
        gfx_text_center(g, 60, buf, 3, true);
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60, sec % 60);
        gfx_text_center(g, 56, buf, 5, true);
    }
    gfx_text_center(g, 112, a->sw_running ? "RUNNING" : "STOPPED", 2, true);
    gfx_text_center(g, 160, "ENTER: START/STOP", 1, true);
    gfx_text_center(g, 172, "UP: RESET", 1, true);
    hint(g, "HOLD ENTER: HOME");
}

/* Inverts the box around one character run drawn at scale s starting at column col. */
static void highlight(gfx_t *g, int x0, int y, int col, int len, int s)
{
    int cell = FONT_W * s;
    gfx_invert(g, x0 + col * cell - 3, y - 3, len * cell - s + 6, 7 * s + 6);
}

static void alarm_screen(const app_t *a, gfx_t *g)
{
    char buf[16];
    title(g, "ALARM");
    const char *state = a->edit_alarm_on ? "ON" : "OFF";
    int sx = (GFX_W - gfx_text_width(state, 3)) / 2;
    gfx_text(g, sx, 44, state, 3, true);
    snprintf(buf, sizeof(buf), "%02u:%02u", a->edit_alarm_h, a->edit_alarm_m);
    int tx = (GFX_W - gfx_text_width(buf, 4)) / 2;
    gfx_text(g, tx, 96, buf, 4, true);
    if (a->edit_field == 0) {
        highlight(g, sx, 44, 0, (int)strlen(state), 3);
    } else {
        highlight(g, tx, 96, a->edit_field == 1 ? 0 : 3, 2, 4);
    }
    gfx_text_center(g, 160, "UP/DOWN: CHANGE", 1, true);
    gfx_text_center(g, 172, "ENTER: NEXT", 1, true);
    hint(g, "HOLD ENTER: CANCEL");
}

static void settime(const app_t *a, gfx_t *g)
{
    char t[16], d[16];
    title(g, "SET TIME");
    snprintf(t, sizeof(t), "%02u:%02u", a->edit.hour, a->edit.minute);
    snprintf(d, sizeof(d), "%02u.%02u.%04u", a->edit.day, a->edit.month, a->edit.year);
    int tx = (GFX_W - gfx_text_width(t, 4)) / 2;
    int dx = (GFX_W - gfx_text_width(d, 2)) / 2;
    gfx_text(g, tx, 48, t, 4, true);
    gfx_text(g, dx, 110, d, 2, true);
    switch (a->edit_field) {
    case 0: highlight(g, tx, 48, 0, 2, 4); break;
    case 1: highlight(g, tx, 48, 3, 2, 4); break;
    case 2: highlight(g, dx, 110, 0, 2, 2); break;
    case 3: highlight(g, dx, 110, 3, 2, 2); break;
    default: highlight(g, dx, 110, 6, 4, 2); break;
    }
    gfx_text_center(g, 160, "UP/DOWN: CHANGE", 1, true);
    gfx_text_center(g, 172, "ENTER: NEXT", 1, true);
    hint(g, "HOLD ENTER: CANCEL");
}

static void about(const app_t *a, gfx_t *g, uint64_t now)
{
    char buf[40];
    title(g, "ABOUT");
    gfx_text(g, 8, 40, "INKTIME FW " FW_VERSION, 1, true);
    gfx_text(g, 8, 54, "NRF52840 BARE-METAL", 1, true);
    static const char *const names[5] = { "EPD", "IMU", "FG ", "CHG", "HAP" };
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "%s %s", names[i], (a->hw_ok & (1u << i)) ? "OK" : "FAIL");
        gfx_text(g, 8 + (i % 2) * 96, 76 + (i / 2) * 14, buf, 1, true);
    }
    /* Minute resolution so the screen does not refresh every second. */
    unsigned long up = (unsigned long)(now / 60000);
    snprintf(buf, sizeof(buf), "UPTIME %lud %02luh %02lum", up / 1440, up / 60 % 24, up % 60);
    gfx_text(g, 8, 126, buf, 1, true);
    snprintf(buf, sizeof(buf), "BATTERY %u MV", a->power.mv);
    gfx_text(g, 8, 140, buf, 1, true);
    hint(g, "ENTER: BACK");
}

/* Greedy word wrap at `cols` characters per line. */
static void wrapped(gfx_t *g, int x, int y, const char *text, int cols, int scale, int max_lines)
{
    char line[40];
    int lines = 0;
    while (*text && lines < max_lines) {
        while (*text == ' ')
            text++;
        int len = (int)strlen(text);
        int take = len <= cols ? len : cols;
        if (len > cols) {
            int sp = take;
            while (sp > 0 && text[sp] != ' ')
                sp--;
            if (sp > 0) take = sp;
        }
        if (take >= (int)sizeof(line)) take = (int)sizeof(line) - 1;
        memcpy(line, text, (size_t)take);
        line[take] = '\0';
        gfx_text(g, x, y + lines * 20, line, scale, true);
        text += take;
        lines++;
    }
}

static void notify(const app_t *a, gfx_t *g)
{
    gfx_icon(g, 92, 8, ICON_MAIL);
    gfx_text_center(g, 30, "MESSAGE", 2, true);
    gfx_hline(g, 10, 52, 180, true);
    wrapped(g, 6, 62, a->notify, 15, 2, 5);
    hint(g, "PRESS ANY BUTTON");
}

static void ringing(const app_t *a, gfx_t *g)
{
    char buf[16];
    gfx_fill(g, 0, 0, GFX_W, GFX_H, true);
    gfx_text_center(g, 40, "ALARM", 4, false);
    snprintf(buf, sizeof(buf), "%02u:%02u", a->alarm_h, a->alarm_m);
    gfx_text_center(g, 100, buf, 4, false);
    gfx_text_center(g, 170, "PRESS ANY BUTTON", 1, false);
}

static void lowbatt(const app_t *a, gfx_t *g)
{
    char buf[16];
    gfx_battery(g, 40, 30, 120, 40, a->power.pct);
    gfx_text_center(g, 90, "LOW BATTERY", 2, true);
    snprintf(buf, sizeof(buf), "%u%%", a->power.pct);
    gfx_text_center(g, 116, buf, 3, true);
    gfx_text_center(g, 156, "CONNECT CHARGER", 1, true);
    hint(g, "PRESS ANY BUTTON");
}

static void off(gfx_t *g)
{
    gfx_text_center(g, 70, "POWER OFF", 3, true);
    gfx_text_center(g, 120, "PRESS ENTER", 1, true);
    gfx_text_center(g, 132, "TO WAKE", 1, true);
}

void app_render(const app_t *a, gfx_t *g, uint64_t now)
{
    gfx_clear(g);
    switch (a->screen) {
    case SCR_WATCHFACE: watchface(a, g, now); break;
    case SCR_MENU: menu(a, g); break;
    case SCR_STEPS: steps(a, g); break;
    case SCR_STOPWATCH: stopwatch(a, g, now); break;
    case SCR_ALARM: alarm_screen(a, g); break;
    case SCR_SETTIME: settime(a, g); break;
    case SCR_BATTERY: battery(a, g); break;
    case SCR_SETTINGS: settings(a, g); break;
    case SCR_ABOUT: about(a, g, now); break;
    case SCR_NOTIFY: notify(a, g); break;
    case SCR_RINGING: ringing(a, g); break;
    case SCR_LOWBATT: lowbatt(a, g); break;
    case SCR_OFF: off(g); break;
    default: break;
    }
}
