/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 button and touch mapping, shared by the device platform
 * (src/q2_platform.c) and the host simulation (tests/host_q2_sim), so the
 * simulation exercises the mapping that ships. No libc: it is also linked
 * into the freestanding browser preview.
 *
 * The transports feed raw events in Q2 Pod's card-app key codes:
 *   170 Return, 171 Play/Pause, 222 Previous, 223 Next, 218 Centre.
 * The device's raw GPIO codes are translated to these first.
 */
#ifndef Q2_INPUT_H
#define Q2_INPUT_H
#include "q2_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { Q2_KEY_RETURN=170, Q2_KEY_PLAY=171, Q2_KEY_PREVIOUS=222,
       Q2_KEY_NEXT=223, Q2_KEY_CENTRE=218 };
/* Return held this long ends the app. */
enum { Q2_QUIT_HOLD_MS=700 };

void q2_input_reset(void);
/* full: all five buttons reach the app, either because Q2 Pod advertises
 * gameview or because the app grabbed the GPIO keys. Pocket Boy's layout
 * then applies everywhere. Otherwise Q2 Pod 1.8.x's fallback applies. */
void q2_input_set_full(int full);
int q2_input_full(void);
void q2_input_set_game(int game);
void q2_input_key(int code, int down, unsigned now);
/* Clockwise travel is positive, with 200 units per revolution. */
void q2_input_wheel(int travel, unsigned now);
void q2_input_set_game_wheel(int on);
int q2_input_game_wheel(void);
void q2_input_set_centre(unsigned button);
unsigned q2_input_centre(void);
/* action: 'd', 'm' or 'u'; x, y in the 375x320 view. */
void q2_input_touch(char action, int x, int y, unsigned now);
/* Q2-Controller's /tmp/q2pad.client report; returns 1 for a valid line. */
int q2_input_pad_report(const char *line, unsigned now);
void q2_input_quit(void);
/* Snapshot since the last call; clears edges. */
q2_input q2_input_take(unsigned now);
/* Logical buttons currently held (for drawing the touch overlay). */
unsigned q2_input_held(void);

/* D-pad, the action complementary to Centre, Menu, and the overlay toggle. */
void q2_overlay_set_visible(int visible);
int q2_overlay_visible(void);
/* Mark a change so the next return to the library saves the choice. */
void q2_overlay_mark_changed(void);
int q2_overlay_take_changed(void);
/* Paints the overlay: plot(ctx, x, y, rgb, alpha 0..256) for each pixel
 * to blend over the game picture, in 375x320 view coordinates. */
typedef void (*q2_plot)(void *ctx, int x, int y, uint32_t rgb, unsigned alpha);
void q2_overlay_paint(q2_plot plot, void *ctx);

#ifdef __cplusplus
}
#endif
#endif
