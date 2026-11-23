/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 input mapping shared by the device and the host simulation.
 *
 * With all five buttons available (gameview firmware, or GPIO keys grabbed
 * as Pocket Boy does on older firmware) the layout follows Pocket Boy
 * (github.com/SlayTheEarth/q2-boy):
 *   in a cart:  Return Up, Play/Pause Down, Previous Left, Next Right,
 *               Centre X, touch O and X.
 *   in menus:   Return Back, Play/Pause X (actions), Previous/Next
 *               Left/Right, Centre O (select).
 *   anywhere:   Return + Centre held 1.2 s ends the app.
 * Without them (Q2 Pod 1.8.x firmware that keeps Return for itself and
 * could not be grabbed) the earlier fallback applies: Back (Return
 * released) opens the menu; Back twice within 500 ms ends the app.
 */
#include "q2_input.h"

enum { ID_RETURN, ID_PLAY, ID_PREVIOUS, ID_NEXT, ID_CENTRE };

/* Layout in the 375x320 view; the 2x game picture is 256x256 at (59, 32),
 * leaving 59 px at each side, so the pads overlap its edges translucently. */
enum { ROUND, PILL };
enum { Q2_TOGGLE = 1u << 16 }; /* internal: show/hide the overlay */
typedef struct {
    int x, y, w, h;
    unsigned key;
    unsigned char kind, always;
    unsigned char glyph[7];
} control;
static const control controls[] = {
    {292, 228, 34, 34, Q2_O,     ROUND, 0, {0,28,34,34,34,28,0}},
    {333, 190, 34, 34, Q2_X,     ROUND, 0, {0,34,20,8,20,34,0}},
    {331, 6,   38, 22, Q2_PAUSE, PILL,  1, {0,62,0,62,0,62,0}},  /* Q2 game menu */
    {6,   6,   38, 22, Q2_TOGGLE,PILL,  1, {0,0,0,0,0,0,0}},     /* show/hide; glyph drawn */
};
enum { CONTROL_COUNT = sizeof controls / sizeof controls[0] };
static unsigned overlay_shown = 1, overlay_changed;

static unsigned full, game, physical, touch_key, raw_keys;
static unsigned combo_at, back_at, back_pending;
static q2_input out;

static void clear_edges(void) {
    out.pressed = 0;
    out.touch_began = 0;
    out.touch_ended = 0;
}
void q2_input_reset(void) {
    physical = touch_key = raw_keys = combo_at = back_at = back_pending = 0;
    out.buttons = 0; out.quit = 0;
    out.touch = out.touch_x = out.touch_y = 0;
    out.touch_start_x = out.touch_start_y = 0;
    clear_edges();
}
void q2_input_set_full(int on) { full = on != 0; }
int q2_input_full(void) { return (int)full; }
void q2_input_set_game(int on) {
    game = on != 0;
    physical = touch_key = raw_keys = combo_at = 0;
    out.buttons = 0;
    out.touch = 0;
    clear_edges();
}
void q2_input_quit(void) { out.quit = 1; }
unsigned q2_input_held(void) { return out.buttons; }

static int key_id(int code) {
    switch (code) {
    case Q2_KEY_RETURN: return ID_RETURN;
    case Q2_KEY_PLAY: return ID_PLAY;
    case Q2_KEY_PREVIOUS: case 172: return ID_PREVIOUS;
    case Q2_KEY_NEXT: case 173: return ID_NEXT;
    case Q2_KEY_CENTRE: return ID_CENTRE;
    }
    return -1;
}

static void hold(unsigned bit, int down) {
    if (down) { out.pressed |= bit & ~physical; physical |= bit; }
    else physical &= ~bit;
}

void q2_input_key(int code, int down, unsigned now) {
    int id = key_id(code);
    if (id < 0) return;
    if (down) raw_keys |= 1u << id; else raw_keys &= ~(1u << id);
    const unsigned combo = (1u << ID_RETURN) | (1u << ID_CENTRE);
    if ((raw_keys & combo) != combo) combo_at = 0;
    else if (!combo_at) combo_at = now ? now : 1u;
    if (!full && id == ID_RETURN) {
        /* Q2 Pod 1.8.x sends Return only as it is released. */
        if (down) return;
        if (back_pending && now - back_at <= 500u) out.quit = 1;
        else out.pressed |= Q2_PAUSE;
        back_pending = 1;
        back_at = now;
        return;
    }
    static const unsigned in_game[] = {Q2_UP, Q2_DOWN, Q2_LEFT, Q2_RIGHT, Q2_X};
    static const unsigned in_menu[] = {Q2_PAUSE, Q2_X, Q2_LEFT, Q2_RIGHT, Q2_O};
    hold(game ? in_game[id] : in_menu[id], down);
}

static int inside(const control *c, int x, int y) {
    if (x < c->x || y < c->y || x >= c->x + c->w || y >= c->y + c->h) return 0;
    if (c->kind == ROUND) {
        /* A touch target a little larger than the drawn circle. */
        int dx = 2 * (x - c->x) - c->w, dy = 2 * (y - c->y) - c->h;
        return dx * dx + dy * dy <= c->w * c->w * 5 / 4;
    }
    return 1;
}
static unsigned control_hit(int x, int y) {
    for (unsigned i = 0; i < CONTROL_COUNT; ++i) {
        const control *c = &controls[i];
        if ((!overlay_shown && !c->always) || !inside(c, x, y)) continue;
        return c->key;
    }
    return 0;
}

void q2_overlay_mark_changed(void) { overlay_changed = 1; }
void q2_overlay_set_visible(int on) { overlay_shown = on != 0; }
int q2_overlay_visible(void) { return (int)overlay_shown; }
int q2_overlay_take_changed(void) {
    unsigned was = overlay_changed;
    overlay_changed = 0;
    return (int)was;
}

void q2_input_touch(char action, int x, int y, unsigned now) {
    (void)now;
    if (action == 'u') {
        out.touch = 0;
        out.touch_ended = 1;
        touch_key = 0;
        return;
    }
    if (action != 'd' && action != 'm') return;
    out.touch_x = x;
    out.touch_y = y;
    if (action == 'd') {
        out.touch = 1;
        out.touch_began = 1;
        out.touch_start_x = x;
        out.touch_start_y = y;
    }
    if (!game) return;
    unsigned key = control_hit(x, y);
    if (key == Q2_TOGGLE) {
        /* Toggles on the touch that lands on it, never by sliding over it. */
        if (action == 'd') {
            overlay_shown = !overlay_shown;
            q2_overlay_mark_changed();
        }
        touch_key = 0;
        return;
    }
    /* Sliding onto an action button presses it. */
    if (key == Q2_PAUSE && action != 'd') key = 0;
    if (key & ~touch_key) out.pressed |= key & ~touch_key;
    touch_key = key;
}

q2_input q2_input_take(unsigned now) {
    if (full && combo_at && (int)(now - combo_at) >= Q2_QUIT_HOLD_MS) out.quit = 1;
    out.buttons = physical | touch_key;
    q2_input snapshot = out;
    clear_edges();
    return snapshot;
}

/* --- drawing --------------------------------------------------------- */
enum { INK = 0xffffff, PAD = 0x5a6480, PAD_ON = 0x29adff, EDGE = 0xc2c3c7 };

static void glyph(q2_plot plot, void *ctx, int cx, int cy, const unsigned char g[7],
                  unsigned alpha) {
    for (int gy = 0; gy < 7; ++gy)
        for (int gx = 0; gx < 7; ++gx)
            if (g[gy] & (1u << (6 - gx)))
                for (int p = 0; p < 4; ++p)
                    plot(ctx, cx - 7 + gx * 2 + (p & 1), cy - 7 + gy * 2 + (p >> 1), INK, alpha);
}

void q2_overlay_paint(q2_plot plot, void *ctx) {
    unsigned held = out.buttons;
    for (unsigned i = 0; i < CONTROL_COUNT; ++i) {
        const control *c = &controls[i];
        if (!overlay_shown && !c->always) continue;
        /* Shown: pads 60% opaque over the game; hidden: a faint hint. */
        unsigned body = overlay_shown ? 154 : 70, ink = overlay_shown ? 235 : 110;
        unsigned on = c->key != Q2_TOGGLE && (held & c->key);
        for (int y = 0; y < c->h; ++y)
            for (int x = 0; x < c->w; ++x) {
                int dx = 2 * x + 1 - c->w, dy = 2 * y + 1 - c->h;
                if (c->kind == ROUND) {
                    int r2 = dx * dx + dy * dy, rr = c->w * c->w;
                    if (r2 > rr) continue;
                    /* Soften the rim. */
                    int rim = r2 > rr - 8 * c->w;
                    plot(ctx, c->x + x, c->y + y, rim ? EDGE : on ? PAD_ON : PAD, on && !rim ? 220 : body);
                } else {
                    /* Pill: rounded ends of radius h/2. */
                    int r = c->h, ex = dx < -(c->w - c->h) ? dx + (c->w - c->h)
                                     : dx > c->w - c->h ? dx - (c->w - c->h) : 0;
                    if (ex * ex + dy * dy > r * r) continue;
                    plot(ctx, c->x + x, c->y + y, on ? PAD_ON : PAD, on ? 220 : body);
                }
            }
        int cx = c->x + c->w / 2, cy = c->y + c->h / 2;
        if (c->key != Q2_TOGGLE) {
            glyph(plot, ctx, cx, cy, c->glyph, ink);
            continue;
        }
        /* Toggle: a small pad outline (overlay shown) or a filled pad. */
        static const unsigned char pad_open[7] = {0,62,73,93,73,62,0};
        static const unsigned char pad_full[7] = {0,62,127,127,127,62,0};
        glyph(plot, ctx, cx, cy, overlay_shown ? pad_open : pad_full, ink);
    }
}
