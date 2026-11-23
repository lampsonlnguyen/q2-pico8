/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 PICO-8 player: the Cover Flow library, then the chosen cart in the
 * upstream FAKE-08 VM, then back to the library when the cart's pause menu
 * says "exit to library". Return + Centre held (or Q2 Pod ending the app)
 * leaves from anywhere.
 *
 * Q2_PICO8_CART=<file> runs one cart from the cart folder without the
 * library and exits when it ends (host simulation and debugging).
 */
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "q2_platform.h"
#include "q2_input.h"
#include "cart_validate.h"
#include "fake08_session.h"
#include "app.h"
#include "library.h"
#include "ui.h"

#include "host.h"
#include "vm.h"
#include "PicoRam.h"
#include "Audio.h"
#include "logger.h"

namespace {
enum Played { PLAYED_LIBRARY, PLAYED_QUIT, PLAYED_ERROR };

std::vector<std::string> wrap(const std::string &text, int width) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush_word = [&]() {
        if (word.empty()) return;
        std::string trial = line.empty() ? word : line + " " + word;
        if (ui::text_width(trial, ui::SMALL) <= width || line.empty()) line = trial;
        else { lines.push_back(line); line = word; }
        word.clear();
    };
    for (char c : ui::ascii(text)) {
        if (c == ' ' || c == '\n') {
            flush_word();
            if (c == '\n' && !line.empty()) { lines.push_back(line); line.clear(); }
        } else word += c;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

/* The cart failed to load: show why until Centre, Return or 60 s. */
void show_cart_error(const std::string &cart, const std::string &error) {
    ui::clear(ui::BLACK);
    ui::caption(28, "This cart could not start", ui::WHITE, ui::LARGE);
    ui::caption(64, ui::fit(ui::ascii(app::title_of(cart)), ui::MEDIUM, ui::W - 40), ui::GRAY, ui::MEDIUM);
    std::vector<std::string> lines = wrap(error.empty() ? "Unknown error" : error, ui::W - 48);
    for (size_t i = 0; i < lines.size() && i < 9; ++i)
        ui::text(24, 104 + (int)i * 20, lines[i], ui::PINK, ui::SMALL);
    ui::caption(292, "Centre or Return: back to the library", ui::DIM, ui::SMALL);
    unsigned start = q2_millis();
    while (true) {
        q2_present_canvas(); /* keeps presenting, as the game would */
        q2_input in = q2_read_input();
        if (in.quit || (in.pressed & (Q2_O | Q2_PAUSE)) || in.touch_ended) {
            if (in.quit) q2_input_quit();
            break;
        }
        if ((unsigned)(q2_millis() - start) > 60000u) break;
        usleep(30000);
    }
}

Played play(const std::string &cart, std::string &message) {
    Played result = PLAYED_LIBRARY;
    Host host;
    /* Vm creates and owns its Memory/Graphics/Input/Audio dependencies.
     * Passing stack allocations with null Graphics/Input makes upstream
     * _cleanupDeps true and causes an invalid free on shutdown. */
    Vm vm(&host, nullptr, nullptr, nullptr, nullptr);
    host.setUpPaletteColors();
    host.oneTimeSetup(nullptr);
    vm.SetCartList(host.listcarts());
    std::string lua_error;
    /* FAKE-08's Cart opens files under Host::getCartDirectory(). */
    if (!vm.LoadCart(cart, false)) {
        show_cart_error(cart, vm.GetBiosError());
        result = PLAYED_ERROR;
    } else if (!q2_cart_lua_ok(cart, host.getCartDirectory(), lua_error)) {
        /* LoadCart parses sections only; a syntax error would otherwise
         * surface on the first tick, when upstream falls back to its BIOS
         * menu and loses the error (see cart_validate.h). */
        Logger_Write("cart Lua error: %s\n", lua_error.c_str());
        show_cart_error(cart, lua_error);
        result = PLAYED_ERROR;
    } else {
        q2_session_vm = &vm;
        vm.vm_run();
        host.setTargetFps(vm.GetTargetFps());
        q2_set_game_mode(1);
        vm.GameLoop();
        q2_set_game_mode(0);
        q2_session_vm = nullptr;
        if (q2_session_end == Q2_END_QUIT) result = PLAYED_QUIT;
        std::string error = vm.GetBiosError();
        if (!error.empty()) message = ui::ascii(error);
    }
    vm.CloseCart();
    host.oneTimeCleanup();
    return result;
}
} // namespace

#ifdef Q2_GLIBC
int main(int argc, char **argv) {
    q2_set_args(argc, argv);
#else
extern "C" int q2_main(void) {
    int argc = 0;
    char **argv = nullptr;
#endif
    (void)argc;
    app::set_exe(argv ? argv[0] : nullptr);
    /* Logger_Initialize reopens stderr as <data>/pico.log. */
    Logger_Initialize((app::data_dir() + "/").c_str());
    Logger_Write("q2-pico8 startup: executable entered, opening display\n");
    if (q2_open()) {
        /* On a physical Q2 a framebuffer failure is an actual error.
         * Only the headless QEMU smoke test may explicitly opt out. */
        Logger_Write("q2-pico8 startup: framebuffer unavailable; startup aborted\n");
        int code = getenv("Q2_PICO8_ALLOW_NO_FB") ? 0 : 1;
        Logger_Exit();
        return code;
    }
    Logger_Write("q2-pico8 startup: framebuffer and input ready\n");
    int status = 0;
    const char *only = getenv("Q2_PICO8_CART");
#ifdef Q2_FAKE08_SMOKE
    /* CI link test: run tests/audio.p8 (Host looks in tests/) directly. */
    if (!only || !*only) only = "audio.p8";
#endif
    if (only && *only) {
        std::string message;
        Played played = play(only, message);
        status = played == PLAYED_ERROR ? 2 : 0;
    } else {
        /* Paint a visible frame before scanning carts, decoding PNG covers,
         * sorting or allocating the Cover Flow cache. If these steps fail on
         * real hardware the screen and pico.log identify how far we got. */
        ui::clear(ui::BLACK);
        ui::caption(85, "PICO-8", ui::WHITE, ui::LARGE);
        ui::caption(137, "Opening game library...", ui::GRAY, ui::MEDIUM);
        q2_present_canvas();
        Logger_Write("q2-pico8 startup: first framebuffer frame submitted\n");
        std::string last, message;
        while (true) {
            Logger_Write("q2-pico8 startup: entering library loop\n");
            LibraryChoice choice = library_run(last, message);
            message.clear();
            if (choice.exit) break;
            last = choice.cart;
            library_played(choice.cart);
            if (play(choice.cart, message) == PLAYED_QUIT) break;
        }
    }
    q2_close(); /* also closes the audio device and releases grabbed keys */
    /* Vm/Audio/Host destructors have run (play() returned), before
     * Logger_Exit() closes stderr. */
    Logger_Exit();
    return status;
}
