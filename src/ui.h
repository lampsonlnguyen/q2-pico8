/* SPDX-License-Identifier: GPL-3.0-or-later
 * Drawing on the 375x320 native canvas: rectangles and antialiased text in
 * Pocket Boy's Liberation Sans masks (15, 20 and 24 px). */
#ifndef Q2_UI_H
#define Q2_UI_H
#include <stdint.h>
#include <string>
#include "q2_platform.h"

namespace ui {
enum { W = Q2_WIDTH, H = Q2_HEIGHT };
enum Size { SMALL, MEDIUM, LARGE };

/* Q2 Pod Cover Flow palette: black shelf, white and gray type. */
constexpr uint32_t BLACK = 0x000000, WHITE = 0xffffff, GRAY = 0xaaaaaa,
                   DIM = 0x777777, BUTTON = 0x242424, BUTTON_OFF = 0x141414,
                   ROW = 0x181818, ROW_ON = 0x393939, GREEN = 0x00e436,
                   PINK = 0xff77a8, RED = 0xff004d;

uint32_t *canvas();
void clear(uint32_t color);
void fill(int x, int y, int w, int h, uint32_t color);
/* Blend color over the area with alpha 0..256. */
void shade(int x, int y, int w, int h, uint32_t color, unsigned alpha);
uint32_t blend(uint32_t under, uint32_t over, unsigned alpha);
int text_width(const std::string &text, Size size);
int line_height(Size size);
/* Draws text from x, clipped to [left, right); returns the end x. */
int text(int x, int y, const std::string &text, uint32_t ink, Size size,
         int left = 0, int right = W);
/* Centred caption; text wider than the margins scrolls by offset px. */
void caption(int y, const std::string &text, uint32_t ink, Size size, int offset = 0,
             int edge = 18);
/* Text ellipsised with "..." to fit width. */
std::string fit(const std::string &text, Size size, int width);
void button(int x, int y, int w, int h, const std::string &label, bool enabled, bool focused);
void ellipsis(int x, int y, uint32_t ink);
/* Pocket Boy's marquee: pause, scroll to the end, pause, scroll back. */
int marquee(int overflow, unsigned elapsed_ms);
/* UTF-8 to the ASCII the masks cover; other characters become '?'. */
std::string ascii(const std::string &utf8);
}
#endif
