/* SPDX-License-Identifier: GPL-3.0-or-later
 * Cover Flow cart library: the app's home screen. */
#ifndef Q2_LIBRARY_H
#define Q2_LIBRARY_H
#include <string>

struct LibraryChoice {
    bool exit = false;  /* leave the app */
    std::string cart;   /* file name in app::cart_dir() to play */
};

/* Runs until a cart is chosen or the user leaves. select: a cart file name
 * to centre on (the last one played); message: shown briefly on entry. */
LibraryChoice library_run(const std::string &select, const std::string &message = "");
/* Puts the cart at the front of Recently played. */
void library_played(const std::string &cart);
/* Flush control changes when returning to the shelf or leaving the app. */
void library_save_controls();
#endif
