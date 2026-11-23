/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 input mapping shared by the device and the host simulation.
 *
 * With all five buttons available (gameview firmware, or GPIO keys grabbed
 * as Pocket Boy does on older firmware) the layout follows Pocket Boy
 * (github.com/SlayTheEarth/q2-boy):
 *   in a cart:  Return Up, Play/Pause Down, Previous Left, Next Right,
 *               Centre O; the wheel holds Left/Right while it turns.
 *   in menus:   Return Back, Play/Pause X (actions), Previous/Next
 *               Left/Right, Centre O (select); the wheel scrolls.
 *   anywhere:   Return + Centre held 1.2 s ends the app.
 * Without them (Q2 Pod 1.8.x firmware that keeps Return for itself and
 * could not be grabbed) the earlier fallback applies: Back (Return
 * released) pauses, Back twice within 500 ms ends the app, Play is X.
 */
#include "q2_input.h"

enum { ID_RETURN, ID_PLAY, ID_PREVIOUS, ID_NEXT, ID_CENTRE };

static const q2_control controls[] = {
    {12, 70,  Q2_UP,    {8,28,62,8,8,8,8}},
    {12, 120, Q2_LEFT,  {0,8,16,62,16,8,0}},
    {12, 170, Q2_RIGHT, {0,8,4,62,4,8,0}},
    {12, 220, Q2_DOWN,  {8,8,8,8,62,28,8}},
    {331,120, Q2_O,     {0,28,34,34,34,28,0}},
    {331,170, Q2_X,     {0,34,20,8,20,34,0}},
    {331,220, Q2_PAUSE, {0,62,0,62,0,62,0}},   /* MENU: PICO-8's pause menu */
};
unsigned q2_control_count(void) { return sizeof controls / sizeof controls[0]; }
const q2_control *q2_control_at(unsigned i) {
    return i < q2_control_count() ? &controls[i] : 0;
}

static unsigned full, game, physical, touch_key, raw_keys;
static unsigned combo_at, back_at, back_pending;
static unsigned wheel_bit, wheel_until;
static q2_input out;

static void clear_edges(void) {
    out.pressed = 0;
    out.wheel = 0;
    out.touch_began = 0;
    out.touch_ended = 0;
}
void q2_input_reset(void) {
    physical = touch_key = raw_keys = combo_at = back_at = back_pending = 0;
    wheel_bit = wheel_until = 0;
    out.buttons = 0; out.quit = 0;
    out.touch = out.touch_x = out.touch_y = 0;
    out.touch_start_x = out.touch_start_y = 0;
    clear_edges();
}
void q2_input_set_full(int on) { full = on != 0; }
int q2_input_full(void) { return (int)full; }
void q2_input_set_game(int on) {
    game = on != 0;
    physical = touch_key = raw_keys = combo_at = back_pending = wheel_bit = 0;
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
    else if (!combo_at) combo_at = now | 1u;
    if (full) {
        static const unsigned in_game[] = {Q2_UP, Q2_DOWN, Q2_LEFT, Q2_RIGHT, Q2_O};
        static const unsigned in_menu[] = {Q2_PAUSE, Q2_X, Q2_LEFT, Q2_RIGHT, Q2_O};
        hold(game ? in_game[id] : in_menu[id], down);
        return;
    }
    if (id == ID_RETURN) {
        /* Q2 Pod 1.8.x sends Return only as it is released. */
        if (down) return;
        if (back_pending && now - back_at <= 500u) out.quit = 1;
        else out.pressed |= Q2_PAUSE;
        back_pending = 1;
        back_at = now;
        return;
    }
    static const unsigned fallback[] = {Q2_PAUSE, Q2_X, Q2_LEFT, Q2_RIGHT, Q2_O};
    hold(fallback[id], down);
}

void q2_input_wheel(int travel, unsigned now) {
    if (!travel) return;
    out.wheel += travel;
    unsigned bit = travel > 0 ? Q2_RIGHT : Q2_LEFT;
    if (game) {
        /* In a cart the wheel steers: Left or Right stays held while it
         * turns (and ~120 ms after), so btn() sees a held direction
         * instead of one-frame taps; btnp() gets the first edge. */
        if (wheel_bit != bit || (int)(now - wheel_until) > 0) out.pressed |= bit;
        wheel_bit = bit;
        unsigned size = (unsigned)(travel < 0 ? -travel : travel);
        unsigned span = 120u + size * 4u;
        wheel_until = now + (span > 300u ? 300u : span);
        return;
    }
    /* Menus (the library, Get Games) read the travel itself, so a turn is
     * not also reported as Left/Right presses. */
}

static unsigned control_hit(int x, int y) {
    for (unsigned i = 0; i < q2_control_count(); ++i)
        if (x >= controls[i].x && x < controls[i].x + Q2_CONTROL_SIZE &&
            y >= controls[i].y && y < controls[i].y + Q2_CONTROL_SIZE)
            return controls[i].key;
    return 0;
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
    /* Sliding onto another control presses it, like a held D-pad. */
    if (key && key != touch_key) out.pressed |= key;
    if (action == 'd' || key) touch_key = key;
}

q2_input q2_input_take(unsigned now) {
    unsigned wheel_held = 0;
    if (wheel_bit && (int)(now - wheel_until) < 0) wheel_held = wheel_bit;
    else wheel_bit = 0;
    if (full && combo_at && now - combo_at >= Q2_QUIT_HOLD_MS) out.quit = 1;
    out.buttons = physical | touch_key | wheel_held;
    q2_input snapshot = out;
    clear_edges();
    return snapshot;
}
