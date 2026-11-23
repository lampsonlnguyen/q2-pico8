/* SPDX-License-Identifier: GPL-3.0-or-later
 * Get Games: browse itch.io's "made with PICO-8" catalog and download free
 * carts into the library, through the q2-pico8-itch helper. */
#ifndef Q2_GETGAMES_H
#define Q2_GETGAMES_H
#include <string>

/* Returns the file name of the last cart added, or "". */
std::string getgames_run();
#endif
