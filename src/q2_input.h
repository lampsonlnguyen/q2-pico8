/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 button, wheel and touch mapping, shared by the device platform
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
/* Return + Centre held this long ends the app (Q2 Pod's gameview shortcut). */
enum { Q2_QUIT_HOLD_MS=1200 };

void q2_input_reset(void);
/* full: all five buttons reach the app, either because Q2 Pod advertises
 * gameview or because the app grabbed the GPIO keys. Pocket Boy's layout
 * then applies everywhere. Otherwise Q2 Pod 1.8.x's fallback applies. */
void q2_input_set_full(int full);
int q2_input_full(void);
void q2_input_set_game(int game);
void q2_input_key(int code, int down, unsigned now);
/* travel: clockwise positive, 200 a turn. */
void q2_input_wheel(int travel, unsigned now);
/* action: 'd', 'm' or 'u'; x, y in the 375x320 view. */
void q2_input_touch(char action, int x, int y, unsigned now);
void q2_input_quit(void);
/* Snapshot since the last call; clears edges and wheel travel. */
q2_input q2_input_take(unsigned now);
/* Logical buttons currently held (for drawing the touch overlay). */
unsigned q2_input_held(void);

/* On-screen gameplay controls (drawn by the platform's present path). */
enum { Q2_CONTROL_SIZE=32 };
typedef struct { int x, y; unsigned key; unsigned char glyph[7]; } q2_control;
unsigned q2_control_count(void);
const q2_control *q2_control_at(unsigned index);

#ifdef __cplusplus
}
#endif
#endif
