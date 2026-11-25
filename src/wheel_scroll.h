/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// Use the library shelf's clickwheel sensitivity throughout the native menus.
class WheelScroll {
    int travel = 0;
    unsigned last = 0;
public:
    int steps(int delta, unsigned now) {
        if (!delta) return 0;
        if (now - last > 300u) travel = 0;
        last = now;
        travel += delta;
        int steps = travel / 38;
        travel %= 38;
        return steps;
    }
};
