/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host Q2 simulation driver for the PICO-8 player.
 *
 * Runs the REAL player code — src/fake08_host.cpp (Host) and the real
 * FAKE-08 Vm — on x86_64, through the host simulation platform
 * (q2_sim_platform.c) instead of a Q2 device. This mirrors
 * src/fake08_main.cpp's smoke flow, except the cart is chosen from the
 * Q2_SIM_CART environment variable (default audio.p8) instead of being
 * hard-coded, and the run ends when the sim platform quits (scripted
 * double-Back, or Q2_SIM_MAX_FRAMES presented frames).
 *
 * Exit codes mirror fake08_main.cpp: 0 ok, 1 host failed to open,
 * 2 cart failed to load (invalid cart fails visibly, no bundled
 * default is played).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "q2_platform.h"
#include "cart_browser.h"
#include "cart_validate.h"
#include "host.h"
#include "vm.h"
#include "PicoRam.h"
#include "Audio.h"
#include "logger.h"

/* PICO-8 16-colour palette, same as src/fake08_main.cpp — the error
 * screen is presented with q2_present(), not the FAKE-08 path. */
static const uint32_t palette[16] = {
    0x000000,0x1d2b53,0x7e2553,0x008751,
    0xab5236,0x5f574f,0xc2c3c7,0xfff1e8,
    0xff004d,0xffa300,0xffec27,0x00e436,
    0x29adff,0x83769c,0xff77a8,0xffccaa
};

/* Mirror of src/fake08_main.cpp's show_cart_load_error: present the
 * error screen in a loop and leave on Centre, Back, quit, or the
 * sim's max-frames cap (q2_read_input reports quit then). Returns 2. */
static int show_cart_load_error(const char *name, const std::string &error) {
    uint8_t screen[8192];
    q2_draw_cart_error(screen, name, error.c_str());
    unsigned start = q2_millis();
    while (1) {
        q2_present(screen, palette);
        q2_input in = q2_read_input();
        if (in.quit || (in.pressed & (Q2_O | Q2_PAUSE))) break;
        if ((unsigned)(q2_millis() - start) > 60000u) break;
        usleep(16000);
    }
    return 2;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *cart = getenv("Q2_SIM_CART");
    if (!cart || !cart[0]) cart = "audio.p8";
    char path[160] = {};
    std::snprintf(path, sizeof path, "%s", cart);

    int result = 0;
    {
        Host host;
        Logger_Initialize(host.logFilePrefix());
        Vm vm(&host, nullptr, nullptr, nullptr, nullptr);
        host.setUpPaletteColors();
        host.oneTimeSetup(nullptr);
        if (host.shouldQuit()) result = 1;
        else {
            vm.SetCartList(host.listcarts());
            fprintf(stderr, "q2-sim: cart dir %s, %zu carts listed\n",
                    host.getCartDirectory().c_str(), host.listcarts().size());
            for (const auto &c : host.listcarts()) fprintf(stderr, "q2-sim:   cart %s\n", c.c_str());
            const char *filename = path;
            for (const char *p = path; *p; ++p) if (*p == '/') filename = p + 1;
            fprintf(stderr, "q2-sim: loading cart %s (target fps %d)\n", filename, vm.GetTargetFps());
            if (!vm.LoadCart(filename, false)) {
                fprintf(stderr, "q2-sim: cart load failed: %s\n", vm.GetBiosError().c_str());
                result = 2;
            } else {
                /* Pre-validate Lua before the VM starts: a syntax
                 * error would otherwise surface on the first
                 * GameLoop tick, when upstream queues the BIOS menu
                 * and the error is lost (see src/cart_validate.h). */
                std::string lua_error;
                if (!q2_cart_lua_ok(filename, host.getCartDirectory(), lua_error)) {
                    fprintf(stderr, "q2-sim: cart Lua error: %s\n", lua_error.c_str());
                    result = show_cart_load_error(filename, lua_error);
                } else {
                    vm.vm_run();
                    host.setTargetFps(vm.GetTargetFps());
                    fprintf(stderr, "q2-sim: running, target fps %d\n", vm.GetTargetFps());
                    vm.GameLoop();
                    fprintf(stderr, "q2-sim: GameLoop ended, frames %d\n", vm.GetFrameCount());
                }
            }
        }
        vm.CloseCart();
        host.oneTimeCleanup();
    }
    Logger_Exit();
    fprintf(stderr, "q2-sim: exit %d\n", result);
    return result;
}
