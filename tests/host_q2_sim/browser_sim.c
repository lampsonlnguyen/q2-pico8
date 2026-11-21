/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host Q2 simulation driver for the cartridge browser picker.
 *
 * Replicates pick_cart() from src/fake08_main.cpp against the REAL
 * src/cart_browser.c (via the sim platform's getdents shim) and the
 * sim framebuffer/input: wheel moves the selection, Centre (O) chooses,
 * Back exits. Prints the cart list and the chosen path, so the run
 * script can verify wheel selection actually changed the selection
 * and O picked the highlighted cart.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "q2_platform.h"
#include "cart_browser.h"

static q2_cart_list carts;
static uint8_t browser_image[8192];
static const uint32_t palette[16] = {
    0x000000,0x1d2b53,0x7e2553,0x008751,
    0xab5236,0x5f574f,0xc2c3c7,0xfff1e8,
    0xff004d,0xffa300,0xffec27,0x00e436,
    0x29adff,0x83769c,0xff77a8,0xffccaa
};

int main(void) {
    if (q2_open()) { fprintf(stderr, "browser-sim: q2_open failed\n"); return 1; }
    q2_find_carts(&carts);
    printf("BROWSER count=%u\n", carts.count);
    for (unsigned i = 0; i < carts.count; ++i)
        printf("BROWSER cart[%u]=%s\n", i, carts.names[i]);
    unsigned dirty = 1, last = 0;
    int chosen = 0;
    char path[160] = {};
    while (1) {
        q2_input input = q2_read_input();
        if (input.quit || (input.pressed & Q2_PAUSE)) break;
        if (input.wheel && carts.count) {
            if (input.wheel > 0) carts.selected = (carts.selected + 1u) % carts.count;
            else carts.selected = (carts.selected + carts.count - 1u) % carts.count;
            printf("BROWSER selected=%u (%s)\n", carts.selected, carts.names[carts.selected]);
            dirty = 1;
        }
        if (input.pressed & Q2_O) {
            if (carts.count) { chosen = q2_cart_path(&carts, path, sizeof path); break; }
            q2_find_carts(&carts);
            dirty = 1;
        }
        if (dirty) {
            q2_draw_cart_browser(browser_image, &carts, 0);
            q2_present(browser_image, palette);
            dirty = 0;
        }
        unsigned now = q2_millis();
        if ((unsigned)(now - last) > 10000) {
            unsigned previous = carts.count;
            q2_find_carts(&carts);
            if (carts.count != previous) dirty = 1;
            last = now;
        }
        usleep(16000);
    }
    printf("BROWSER chosen=%d path=%s selected=%u\n", chosen, path, carts.selected);
    q2_close();
    return chosen ? 0 : 1;
}
