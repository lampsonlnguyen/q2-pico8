/* SPDX-License-Identifier: GPL-3.0-or-later
 * Text and shape drawing adapted from Pocket Boy's frontend
 * (github.com/SlayTheEarth/q2-boy, GPL-2.0-or-later). */
#include "ui.h"
#include "ui_font.h"

namespace ui {
namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int glyph(unsigned char ch) { return ch < 32 || ch > 126 ? '?' - 32 : ch - 32; }
const unsigned char *advances(Size s) {
    return s == SMALL ? advance : s == MEDIUM ? advance_20 : advance_24;
}
void draw_mask(int x, int y, int width, int height, const unsigned char *mask, uint32_t ink,
               int left, int right) {
    uint32_t *px = canvas();
    const int stride = (width + 1) / 2;
    int first_col = clampi(left - x, 0, width), end_col = clampi(right - x, 0, width);
    int first_row = clampi(-y, 0, height), end_row = clampi(H - y, 0, height);
    for (int row = first_row; row < end_row; ++row)
        for (int col = first_col; col < end_col; ++col) {
            unsigned alpha = (mask[row * stride + col / 2] >> (4 * (col % 2))) & 15u;
            if (!alpha) continue;
            uint32_t *p = &px[(y + row) * W + x + col];
            *p = alpha == 15 ? ink : blend(*p, ink, alpha * 17 + (alpha * 17 >> 7));
        }
}
} // namespace

uint32_t *canvas() { return q2_canvas(); }

uint32_t blend(uint32_t under, uint32_t over, unsigned a) {
    if (a >= 256) return over;
    unsigned rb = ((over & 0xff00ff) * a + (under & 0xff00ff) * (256 - a)) >> 8;
    unsigned g = ((over & 0x00ff00) * a + (under & 0x00ff00) * (256 - a)) >> 8;
    return (rb & 0xff00ff) | (g & 0xff00);
}

void clear(uint32_t color) {
    uint32_t *px = canvas();
    for (int i = 0; i < W * H; ++i) px[i] = color;
}

void fill(int x, int y, int w, int h, uint32_t color) {
    int l = clampi(x, 0, W), r = clampi(x + w, 0, W), t = clampi(y, 0, H), b = clampi(y + h, 0, H);
    uint32_t *px = canvas();
    for (int row = t; row < b; ++row)
        for (int col = l; col < r; ++col) px[row * W + col] = color;
}

void shade(int x, int y, int w, int h, uint32_t color, unsigned alpha) {
    int l = clampi(x, 0, W), r = clampi(x + w, 0, W), t = clampi(y, 0, H), b = clampi(y + h, 0, H);
    uint32_t *px = canvas();
    for (int row = t; row < b; ++row)
        for (int col = l; col < r; ++col) px[row * W + col] = blend(px[row * W + col], color, alpha);
}

int line_height(Size s) { return s == SMALL ? 17 : s == MEDIUM ? 24 : 28; }

int text_width(const std::string &t, Size s) {
    const unsigned char *adv = advances(s);
    int w = 0;
    for (unsigned char ch : t) w += adv[glyph(ch)];
    return w;
}

int text(int x, int y, const std::string &t, uint32_t ink, Size s, int left, int right) {
    const unsigned char *adv = advances(s);
    for (unsigned char ch : t) {
        int g = glyph(ch);
        if (x >= right) break;
        if (x + adv[g] > left) {
            if (s == SMALL) draw_mask(x, y, 16, 17, &glyphs[g][0][0], ink, left, right);
            else if (s == MEDIUM) draw_mask(x, y, 26, 24, &glyphs_20[g][0][0], ink, left, right);
            else draw_mask(x, y, 26, 28, &glyphs_24[g][0][0], ink, left, right);
        }
        x += adv[g];
    }
    return x;
}

void caption(int y, const std::string &t, uint32_t ink, Size s, int offset, int edge) {
    int w = text_width(t, s), x = (W - w) / 2;
    if (w > W - edge * 2) x = edge - offset;
    text(x, y, t, ink, s, edge, W - edge);
}

std::string fit(const std::string &t, Size s, int width) {
    if (text_width(t, s) <= width) return t;
    std::string out = t;
    while (!out.empty() && text_width(out + "...", s) > width) out.pop_back();
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "...";
}

void button(int x, int y, int w, int h, const std::string &label, bool enabled, bool focused) {
    fill(x, y, w, h, focused ? ROW_ON : enabled ? BUTTON : BUTTON_OFF);
    if (focused) {
        fill(x, y, w, 2, WHITE);
        fill(x, y + h - 2, w, 2, WHITE);
    }
    std::string shown = fit(label, SMALL, w - 8);
    text(x + (w - text_width(shown, SMALL)) / 2, y + (h - 17) / 2, shown,
         enabled ? WHITE : DIM, SMALL, x + 2, x + w - 2);
}

void ellipsis(int x, int y, uint32_t ink) {
    for (int dx = -7; dx <= 7; dx += 7) {
        fill(x + dx - 3, y - 3, 6, 6, 0x101010);
        fill(x + dx - 2, y - 2, 4, 4, ink);
    }
}

int marquee(int overflow, unsigned ms) {
    enum { WAIT = 1500, SPEED = 30 }; /* px per second */
    if (overflow <= 0) return 0;
    unsigned sweep = (unsigned)(overflow * 1000 + SPEED - 1) / SPEED;
    unsigned cycle = 2 * (sweep + WAIT);
    unsigned at = ms % cycle;
    if (at < WAIT) return 0;
    at -= WAIT;
    if (at < sweep) return (int)(at * SPEED / 1000);
    at -= sweep;
    if (at < WAIT) return overflow;
    at -= WAIT;
    return at < sweep ? overflow - (int)(at * SPEED / 1000) : 0;
}

std::string ascii(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 0x80) { out += ch >= 32 ? (char)ch : ' '; continue; }
        size_t extra = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : ch >= 0xc0 ? 1 : 0;
        /* A few common typographic marks get ASCII stand-ins. */
        if (extra == 2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 && ch == 0xe2) {
            unsigned char c3 = (unsigned char)s[i + 2];
            if (c3 == 0x93 || c3 == 0x94) out += '-';
            else if (c3 == 0x98 || c3 == 0x99) out += '\'';
            else if (c3 == 0x9c || c3 == 0x9d) out += '"';
            else if (c3 == 0xa6) out += "...";
            else out += '?';
        } else out += '?';
        i += extra;
    }
    return out;
}
}
