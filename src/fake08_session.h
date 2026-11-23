/* SPDX-License-Identifier: GPL-3.0-or-later
 * State shared between the player loop (fake08_main.cpp) and the Host. */
#ifndef Q2_FAKE08_SESSION_H
#define Q2_FAKE08_SESSION_H
class Vm;
/* How a cart's GameLoop ended. */
enum { Q2_END_NONE, Q2_END_LIBRARY, Q2_END_QUIT };
extern Vm *q2_session_vm;  /* the running cart's VM, or null */
void q2_configure_audio(int argc);
extern int q2_session_end;
#endif
