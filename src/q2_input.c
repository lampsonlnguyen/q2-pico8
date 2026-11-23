/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 input mapping shared by the device and the host simulation.
 *
 * With all five buttons available (gameview firmware, or GPIO keys grabbed
 * as Pocket Boy does on older firmware) the layout follows Pocket Boy
 * (github.com/SlayTheEarth/q2-boy):
 *   in a cart:  Return Up, Play/Pause Down, Previous Left, Next Right.
 *               Centre is O or X; the overlay provides the other action.
 *   in menus:   Return Back, Play/Pause actions, Centre select.
 *   anywhere:   hold Return to quit.
 * Without them (Q2 Pod 1.8.x firmware that keeps Return for itself and
 * could not be grabbed) the earlier fallback applies: Back (Return
 * released) opens the menu; Back twice within 500 ms ends the app.
 */
#include "q2_input.h"

enum { ID_RETURN, ID_PLAY, ID_PREVIOUS, ID_NEXT, ID_CENTRE };

/* Layout in the 375x320 view; the 2x game picture is 256x256 at (59, 32),
 * leaving 59 px at each side, so the pads overlap its edges translucently. */
enum { DPAD, ROUND, PILL };
enum { Q2_TOGGLE = 1u << 16 }; /* internal: show/hide the overlay */
typedef struct {
    int x, y, w, h;
    unsigned key;
    unsigned char kind, always;
    unsigned char glyph[7];
} control;
static const control controls[] = {
    {4,   190, 78, 78, 0,        DPAD,  0, {0}},
    {292, 228, 34, 34, Q2_O,     ROUND, 0, {0,28,34,34,34,28,0}},
    {333, 190, 34, 34, Q2_X,     ROUND, 0, {0,34,20,8,20,34,0}},
    {237, 14,  38, 22, Q2_PAUSE, PILL,  1, {0,62,0,62,0,62,0}},  /* Q2 game menu */
    {100, 14,  38, 22, Q2_TOGGLE,PILL,  1, {0,0,0,0,0,0,0}},     /* show/hide; glyph drawn */
};
enum { CONTROL_COUNT = sizeof controls / sizeof controls[0] };
static const unsigned char arrows[4][7] = {
    {8,28,62,8,8,8,8},     /* up */
    {0,8,16,62,16,8,0},    /* left */
    {0,8,4,62,4,8,0},      /* right */
    {8,8,8,8,62,28,8},     /* down */
};
static unsigned overlay_shown = 1, overlay_changed;
static unsigned centre = Q2_X, game_wheel;

static unsigned full, game, physical, touch_key;
static unsigned return_down, return_back, return_at, back_at, back_pending;
static unsigned centre_down, centre_edge;
static unsigned wheel_bit, wheel_until;
static q2_input out;

static void clear_edges(void) {
    out.pressed = 0;
    out.wheel = 0;
    centre_edge = 0;
    out.touch_began = 0;
    out.touch_ended = 0;
}
void q2_input_reset(void) {
    physical = touch_key = wheel_bit = 0;
    return_down = return_back = return_at = back_at = back_pending = centre_down = 0;
    out.buttons = 0; out.quit = 0;
    out.touch = out.touch_x = out.touch_y = 0;
    out.touch_start_x = out.touch_start_y = 0;
    clear_edges();
}
void q2_input_set_full(int on) { full = on != 0; }
int q2_input_full(void) { return (int)full; }
void q2_input_set_game(int on) {
    game = on != 0;
    physical = touch_key = wheel_bit = 0;
    return_back = 0;
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
    if (id == ID_CENTRE) {
        centre_down = down != 0;
        if (down && !game) {
            /* Firmware can send a pointer release when Centre is pressed. */
            centre_edge = 1;
            touch_key = 0;
            out.touch = out.touch_began = out.touch_ended = 0;
        }
    }
    if (id == ID_RETURN) {
        if (down) {
            if (!return_down) {
                return_down = 1;
                return_at = now;
                return_back = !game || !full;
            }
        } else {
            if (return_down && now - return_at >= Q2_QUIT_HOLD_MS) out.quit = 1;
            if (return_back || (!full && !return_down)) {
                if (!full && back_pending && now - back_at <= 500u) out.quit = 1;
                else out.pressed |= Q2_PAUSE;
                back_pending = !full;
                back_at = now;
            }
            return_down = return_back = 0;
        }
        if (game && full) hold(Q2_UP, down);
        return;
    }
    static const unsigned directions[] = {0, Q2_DOWN, Q2_LEFT, Q2_RIGHT, 0};
    unsigned bit = directions[id];
    if (id == ID_CENTRE) bit = game ? centre : Q2_O;
    if (id == ID_PLAY && !game) bit = Q2_X;
    hold(bit, down);
}

void q2_input_wheel(int travel, unsigned now) {
    if (!travel) return;
    if (travel > 200) travel = 200;
    if (travel < -200) travel = -200;
    if (!game) {
        out.wheel += travel;
        if (out.wheel > 200) out.wheel = 200;
        if (out.wheel < -200) out.wheel = -200;
        return;
    }
    if (!game_wheel) return;
    unsigned bit = travel > 0 ? Q2_RIGHT : Q2_LEFT;
    if (wheel_bit != bit || (int)(now - wheel_until) >= 0)
        out.pressed |= bit & ~(physical | touch_key);
    wheel_bit = bit;
    int span = 120 + 4 * (travel > 0 ? travel : -travel);
    wheel_until = now + (span < 300 ? span : 300);
}

void q2_input_set_game_wheel(int on) {
    game_wheel = on != 0;
    wheel_bit = 0;
}
int q2_input_game_wheel(void) { return (int)game_wheel; }

void q2_input_set_centre(unsigned button) { centre = button == Q2_O ? Q2_O : Q2_X; }
unsigned q2_input_centre(void) { return centre; }

static int control_visible(const control *c) {
    return c->always || (overlay_shown && c->key != centre);
}

/* The D-pad is a 3x3 grid: arms give one direction, corners two. */
static unsigned dpad_keys(const control *c, int x, int y) {
    int cell = c->w / 3;
    unsigned keys = 0;
    if (x < c->x + cell) keys |= Q2_LEFT;
    if (x >= c->x + 2 * cell) keys |= Q2_RIGHT;
    if (y < c->y + cell) keys |= Q2_UP;
    if (y >= c->y + 2 * cell) keys |= Q2_DOWN;
    return keys;
}
static int inside(const control *c, int x, int y) {
    /* Keep the artwork compact and give taps extra room around it. */
    const int padding = c->kind == DPAD ? 8 : 11;
    if (x < c->x - padding || y < c->y - padding ||
        x >= c->x + c->w + padding || y >= c->y + c->h + padding) return 0;
    if (c->kind == ROUND) {
        int dx = 2 * (x - c->x) - c->w, dy = 2 * (y - c->y) - c->h;
        int diameter = c->w + 2 * padding;
        return dx * dx + dy * dy <= diameter * diameter;
    }
    return 1;
}
static unsigned control_hit(int x, int y) {
    for (unsigned i = 0; i < CONTROL_COUNT; ++i) {
        const control *c = &controls[i];
        if (!control_visible(c) || !inside(c, x, y)) continue;
        return c->kind == DPAD ? dpad_keys(c, x, y) : c->key;
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
    if (!game && (centre_down || centre_edge)) return;
    if (action == 'u') {
        if (!out.touch) return;
        if (x >= 0 && y >= 0) {
            out.touch_x = x;
            out.touch_y = y;
        }
        out.touch = 0;
        out.touch_ended = 1;
        touch_key = 0;
        return;
    }
    if (action != 'd' && (action != 'm' || !out.touch)) return;
    out.touch_x = x;
    out.touch_y = y;
    if (action == 'd') {
        out.touch = 1;
        out.touch_began = 1;
        out.touch_ended = 0;
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
    out.pressed |= key & ~(touch_key | physical | wheel_bit);
    touch_key = key;
}

q2_input q2_input_take(unsigned now) {
    if (return_down && now - return_at >= Q2_QUIT_HOLD_MS) out.quit = 1;
    if (wheel_bit && (int)(now - wheel_until) >= 0) wheel_bit = 0;
    out.buttons = physical | touch_key | wheel_bit;
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
        if (!control_visible(c)) continue;
        /* Shown: pads 60% opaque over the game; hidden: a faint hint. */
        unsigned body = overlay_shown ? 154 : 70, ink = overlay_shown ? 235 : 110;
        if (c->kind == DPAD) {
            int cell = c->w / 3;
            /* The cross: centre plus four arms, each lit while held. */
            static const int arm[5][2] = {{1,1},{1,0},{0,1},{2,1},{1,2}};
            static const unsigned arm_key[5] = {0, Q2_UP, Q2_LEFT, Q2_RIGHT, Q2_DOWN};
            for (int a = 0; a < 5; ++a) {
                unsigned on = arm_key[a] && (held & arm_key[a]);
                int ax = c->x + arm[a][0] * cell, ay = c->y + arm[a][1] * cell;
                for (int y = 0; y < cell; ++y)
                    for (int x = 0; x < cell; ++x) {
                        /* A light rim along the cross's outline: an arm's two
                         * long sides and its outer end; none on the centre. */
                        int rim = 0, last = cell - 1;
                        if (arm[a][0] == 1 && arm[a][1] != 1)      /* up or down */
                            rim = x == 0 || x == last || y == (arm[a][1] ? last : 0);
                        else if (arm[a][1] == 1 && arm[a][0] != 1) /* left or right */
                            rim = y == 0 || y == last || x == (arm[a][0] ? last : 0);
                        plot(ctx, ax + x, ay + y, rim ? EDGE : on ? PAD_ON : PAD,
                             rim ? body : on ? 220 : body);
                    }
                if (a) glyph(plot, ctx, c->x + arm[a][0] * cell + cell / 2,
                             c->y + arm[a][1] * cell + cell / 2, arrows[a - 1], ink);
            }
            continue;
        }
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
