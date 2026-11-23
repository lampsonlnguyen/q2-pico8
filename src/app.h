/* SPDX-License-Identifier: GPL-3.0-or-later
 * Paths and small file helpers shared by the library screens. */
#ifndef Q2_APP_H
#define Q2_APP_H
#include <string>
#include <vector>

namespace app {
/* /mnt/mmc/.pico8 on the Q2 (Q2_PICO8_DATA overrides it for tests). */
const std::string &data_dir();
std::string cart_dir();   /* data_dir()/carts */
std::string art_dir();    /* data_dir()/covers */
std::string helper();     /* q2-pico8-itch beside the executable */
void set_exe(const char *argv0);
/* "Celeste Classic [itch 42].p8.png" -> "Celeste Classic". */
std::string title_of(const std::string &file);
bool is_cart(const std::string &file);
std::vector<std::string> list_carts();
std::vector<std::string> read_lines(const std::string &path, size_t limit);
bool write_lines(const std::string &path, const std::vector<std::string> &lines);
/* Ticks of wheel travel: Pocket Boy's Cover Flow uses 38 of 200 a turn. */
int wheel_steps(int &accumulator, unsigned &last_ms, int travel, unsigned now, int tick);
}
#endif
