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
#include <vector>

#include "q2_platform.h"
#include "host.h"
#include "vm.h"
#include "PicoRam.h"
#include "Audio.h"
#include "logger.h"

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
                vm.vm_run();
                host.setTargetFps(vm.GetTargetFps());
                fprintf(stderr, "q2-sim: running, target fps %d\n", vm.GetTargetFps());
                vm.GameLoop();
                fprintf(stderr, "q2-sim: GameLoop ended, frames %d\n", vm.GetFrameCount());
            }
        }
        vm.CloseCart();
        host.oneTimeCleanup();
    }
    Logger_Exit();
    fprintf(stderr, "q2-sim: exit %d\n", result);
    return result;
}
