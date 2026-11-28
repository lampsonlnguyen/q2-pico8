/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "q2_input.h"
#include <assert.h>

int main(void) {
    q2_input_reset();
    q2_input_set_full(1);
    q2_input_set_game(1);
    assert(q2_input_pad_report("p 1 0 0 0 0 0 0\n", 1));
    assert(q2_input_take(1).buttons == Q2_O);
    assert(q2_input_pad_report("p 2 0 0 0 0 0 0\n", 2));
    assert(q2_input_take(2).buttons == Q2_X);
    assert(q2_input_pad_report("p 4 0 0 0 0 0 0\n", 3));
    assert(q2_input_take(3).buttons == Q2_X);
    assert(q2_input_pad_report("p 0 0 0 0 0 0 0\n", 4));
    assert(q2_input_take(4).buttons == 0);
    assert(q2_input_pad_report("p 2803 0 0 0 0 0 0\n", 10));
    q2_input s = q2_input_take(10);
    assert(s.buttons == (Q2_LEFT | Q2_UP | Q2_O | Q2_X));
    assert(s.pressed == s.buttons);
    assert(q2_input_take(11).pressed == 0);
    assert(!q2_input_pad_report("p 1 999999 0 0 0 0 0\n", 12));
    assert(!q2_input_pad_report("p 1 0 0 0 0 0 1024\n", 12));
    assert(!q2_input_pad_report("p 1 0 0 0 0 0 0 junk\n", 12));
    assert(q2_input_take(12).buttons == s.buttons);
    q2_input_key(Q2_KEY_NEXT, 1, 13);
    assert(q2_input_pad_report("p 0 0 0 0 0 0 0\n", 14));
    assert(q2_input_take(14).buttons == Q2_RIGHT);
    q2_input_key(Q2_KEY_NEXT, 0, 15);
    assert(q2_input_pad_report("p 0 -32768 32767 0 0 1023 0\n", 16));
    assert(q2_input_take(16).buttons == (Q2_LEFT | Q2_DOWN));
    assert(q2_input_pad_report("p 0 100 -100 0 0 0 0\n", 17));
    assert(q2_input_take(17).buttons == 0);
    assert(q2_input_pad_report("p 80 0 0 0 0 0 0\n", 18));
    assert(q2_input_take(18).pressed == Q2_PAUSE);
    q2_input_set_game(0);
    assert(q2_input_pad_report("p 80 0 0 0 0 0 0\n", 19));
    assert(q2_input_take(19).pressed == 0);
    assert(q2_input_pad_report("p 0 0 0 0 0 0 0\n", 20));
    assert(q2_input_pad_report("p 800 0 0 0 0 0 0\n", 21));
    assert(q2_input_take(21).pressed == Q2_LEFT);
    assert(q2_input_take(100).pressed == 0);
    assert(q2_input_take(371).pressed == Q2_LEFT);
    assert(q2_input_pad_report("p 0 0 0 0 0 0 0\n", 372));
    assert(q2_input_pad_report("p 1 0 0 0 0 0 0\n", 373));
    assert(q2_input_take(373).pressed == Q2_O);
    q2_input_set_game(1);
    assert(q2_input_pad_report("p 1 0 0 0 0 0 0\n", 374));
    assert(q2_input_take(374).buttons == 0);
    assert(q2_input_pad_report("p 0 0 0 0 0 0 0\n", 375));
    assert(q2_input_pad_report("p 1 0 0 0 0 0 0\n", 376));
    assert(q2_input_pad_report("p 0 0 0 0 0 0 0\n", 377));
    s = q2_input_take(378);
    assert(s.pressed == Q2_O && s.buttons == 0);
    return 0;
}
