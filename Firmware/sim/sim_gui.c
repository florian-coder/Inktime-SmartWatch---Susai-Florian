/*
 * SDL2 front-end for the simulator: the e-paper panel inside a watch body, with
 * clickable side buttons. Virtual time follows wall-clock time (x speed).
 *
 *   Up / W         UP button        (hold for long press / auto-repeat)
 *   Enter / Space  ENTER button
 *   Down / S       DOWN button
 *   C              cycle charger: off -> charging -> full -> off
 *   B              battery -10 %          G   battery +10 %
 *   T              walk 50 steps          N   send a notification
 *   F              fast forward x60 on/off
 *   P              save screenshot (screenshot_N.pbm)
 *   Esc            quit
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "core/datetime.h"
#include "models/models.h"
#include "pbm.h"
#include "sim.h"
#include "watch.h"

int sim_gui_run(int scale, double speed, const char *shot);

extern const uint8_t font5x7[95][5];

#define BEZEL    28
#define BTN_W    14
#define BTN_H    44
#define STATUS_H 64

typedef struct {
    SDL_Renderer *ren;
    int panel_x, panel_y, panel_w;
    SDL_Rect btn[BTN_COUNT];
    bool pressed[BTN_COUNT];
    bool mouse_btn[BTN_COUNT];
} gui_t;

static void text(SDL_Renderer *r, int x, int y, const char *s, int px, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    for (; *s; s++, x += 6 * px) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (font5x7[ch - 0x20][col] & (1u << row)) {
                    SDL_Rect d = { x + col * px, y + row * px, px, px };
                    SDL_RenderFillRect(r, &d);
                }
            }
        }
    }
}

static void fill(SDL_Renderer *r, SDL_Rect rc, Uint8 R, Uint8 G, Uint8 B)
{
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_RenderFillRect(r, &rc);
}

static void set_button(gui_t *g, btn_id_t b, bool down)
{
    if (g->pressed[b] != down) {
        g->pressed[b] = down;
        sim_button(b, down);
    }
}

static int key_button(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP:
    case SDLK_w: return BTN_UP;
    case SDLK_RETURN:
    case SDLK_SPACE:
    case SDLK_KP_ENTER: return BTN_ENTER;
    case SDLK_DOWN:
    case SDLK_s: return BTN_DOWN;
    default: return -1;
    }
}

static void draw(gui_t *g, SDL_Texture *tex, int shake, double speed)
{
    SDL_Renderer *r = g->ren;
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    fill(r, (SDL_Rect){ 0, 0, w, h }, 0x2B, 0x2D, 0x31);

    /* watch body */
    SDL_Rect body = { g->panel_x - BEZEL + shake, g->panel_y - BEZEL, g->panel_w + 2 * BEZEL, g->panel_w + 2 * BEZEL };
    fill(r, body, 0x12, 0x12, 0x14);
    fill(r, (SDL_Rect){ body.x + 4, body.y + 4, body.w - 8, body.h - 8 }, 0x1D, 0x1E, 0x21);

    /* buttons on the right edge */
    for (int i = 0; i < BTN_COUNT; i++) {
        SDL_Rect b = g->btn[i];
        b.x += shake;
        bool down = g->pressed[i];
        fill(r, b, down ? 0x5E : 0x3A, down ? 0x9C : 0x3C, down ? 0xFF : 0x40);
    }

    /* e-paper glass */
    SDL_Rect panel = { g->panel_x + shake, g->panel_y, g->panel_w, g->panel_w };
    SDL_RenderCopy(r, tex, NULL, &panel);

    /* status lines */
    const app_t *a = watch_app();
    datetime_t dt;
    dt_from_epoch(app_epoch(a, sim_now()), &dt);
    char l1[96], l2[96];
    snprintf(l1, sizeof(l1), "%04u-%02u-%02u %02u:%02u:%02u  %s  x%.0f%s", dt.year, dt.month, dt.day, dt.hour,
             dt.minute, dt.second, app_screen_name(a->screen), speed, sim_is_off() ? "  SYSTEM OFF" : "");
    snprintf(l2, sizeof(l2), "BAT %u%%%s  STEPS %lu  REFRESH %lu/%lu  HAPTIC %lu", a->power.pct,
             a->power.charging ? "+" : "", (unsigned long)app_steps_today(a), (unsigned long)epdm_stats()->full,
             (unsigned long)epdm_stats()->partial, (unsigned long)hapm_play_count());
    const SDL_Color fg = { 0xD8, 0xD8, 0xD8, 255 }, dim = { 0x8A, 0x8D, 0x93, 255 };
    int ty = body.y + body.h + 12;
    text(r, 12, ty, l1, 2, fg);
    text(r, 12, ty + 20, l2, 2, fg);
    text(r, 12, ty + 40, "ARROWS/ENTER  C:CHARGER B/G:BATTERY T:STEPS N:NOTIFY F:FAST P:SHOT", 1, dim);
    SDL_RenderPresent(r);
}

static void upload_panel(SDL_Texture *tex)
{
    static gfx_t panel;
    static uint32_t px[GFX_W * GFX_H];
    sim_panel(&panel);
    for (int y = 0; y < GFX_H; y++) {
        for (int x = 0; x < GFX_W; x++) {
            px[y * GFX_W + x] = gfx_get(&panel, x, y) ? 0xFF1C1C1Cu : 0xFFE9E7E0u;
        }
    }
    SDL_UpdateTexture(tex, NULL, px, GFX_W * 4);
}

int sim_gui_run(int scale, double speed, const char *shot)
{
    if (scale < 1) scale = 1;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    gui_t g = { 0 };
    g.panel_w = GFX_W * scale;
    g.panel_x = BEZEL + 16;
    g.panel_y = BEZEL + 16;
    const int win_w = g.panel_w + 2 * BEZEL + 32 + BTN_W;
    const int win_h = g.panel_w + 2 * BEZEL + 32 + STATUS_H;
    const int bx = g.panel_x + g.panel_w + BEZEL;
    for (int i = 0; i < BTN_COUNT; i++) {
        g.btn[i] = (SDL_Rect){ bx, g.panel_y + g.panel_w / 6 + i * (g.panel_w / 3) - BTN_H / 2 + g.panel_w / 12, BTN_W,
                               BTN_H };
    }

    SDL_Window *win =
        SDL_CreateWindow("InkTime simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, 0);
    g.ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g.ren) {
        g.ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_Texture *tex = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, GFX_W, GFX_H);
    if (!win || !g.ren || !tex) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }

    sim_log_echo(true);
    sim_init();
    printf("InkTime simulator window: arrows/Enter = buttons, see the hint line for more keys.\n");

    bool quit = false, fast = false;
    int charger = 0, shots = 0;
    uint32_t last_plays = hapm_play_count();
    Uint64 shake_until = 0;
    Uint64 last = SDL_GetTicks64();

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
                const bool down = e.type == SDL_KEYDOWN;
                const int b = key_button(e.key.keysym.sym);
                if (b >= 0) {
                    set_button(&g, (btn_id_t)b, down);
                    continue;
                }
                if (!down) continue;
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: quit = true; break;
                case SDLK_c:
                    charger = (charger + 1) % 3;
                    sim_set_charger(charger != 0, charger == 1   ? CHG_CONSTANT_CURRENT
                                                  : charger == 2 ? CHG_DONE
                                                                 : CHG_NOT_CHARGING);
                    break;
                case SDLK_b:
                case SDLK_g: {
                    int pct = watch_app()->power.pct + (e.key.keysym.sym == SDLK_g ? 10 : -10);
                    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
                    sim_set_battery((uint8_t)pct, (uint16_t)(3300 + pct * 9));
                    break;
                }
                case SDLK_t: sim_add_steps(50); break;
                case SDLK_n: sim_notify("Hello from the simulator! Press any button."); break;
                case SDLK_f: fast = !fast; break;
                case SDLK_p: {
                    static gfx_t panel;
                    char name[64];
                    snprintf(name, sizeof(name), "screenshot_%d.pbm", shots++);
                    sim_panel(&panel);
                    if (pbm_write(name, &panel)) printf("saved %s\n", name);
                    break;
                }
                default: break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                SDL_Point p = { e.button.x, e.button.y };
                for (int i = 0; i < BTN_COUNT; i++) {
                    SDL_Rect hit = { g.btn[i].x - 8, g.btn[i].y - 4, g.btn[i].w + 16, g.btn[i].h + 8 };
                    if (e.type == SDL_MOUSEBUTTONDOWN && SDL_PointInRect(&p, &hit)) {
                        g.mouse_btn[i] = true;
                        set_button(&g, (btn_id_t)i, true);
                    } else if (e.type == SDL_MOUSEBUTTONUP && g.mouse_btn[i]) {
                        g.mouse_btn[i] = false;
                        set_button(&g, (btn_id_t)i, false);
                    }
                }
            }
        }

        const Uint64 now = SDL_GetTicks64();
        Uint64 dt = now - last;
        last = now;
        if (dt > 200) dt = 200;
        const double factor = speed * (fast ? 60.0 : 1.0);
        if (!sim_run_for((uint64_t)((double)dt * factor) + 1)) {
            fprintf(stderr, "simulation stopped: %s\n", sim_model_error_text());
            quit = true;
        }

        if (hapm_play_count() != last_plays) {
            last_plays = hapm_play_count();
            shake_until = now + (hapm_last_effect() == 14 ? 450 : 120);
        }
        const int shake = now < shake_until ? ((now / 30) % 2 ? 3 : -3) : 0;
        upload_panel(tex);
        draw(&g, tex, shake, factor);
        if (shot && sim_now() > 3000) {
            /* --gui-shot: save the rendered window (used for docs and to check the layout) */
            int w, h;
            SDL_GetRendererOutputSize(g.ren, &w, &h);
            SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (surf && SDL_RenderReadPixels(g.ren, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) {
                SDL_SaveBMP(surf, shot);
                printf("saved %s\n", shot);
            }
            SDL_FreeSurface(surf);
            quit = true;
        }
        SDL_Delay(10);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
