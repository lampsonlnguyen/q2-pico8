/* SPDX-License-Identifier: GPL-3.0-or-later
 * Cart artwork, in order of preference:
 *   1. <art dir>/<cart file>.png (itch.io cover saved by Get Games, or the
 *      user's own picture),
 *   2. the cart image itself for .p8.png carts,
 *   3. the __label__ section of .p8 text carts, on a drawn cart body,
 *   4. a plain cart body.
 * Decoded artwork is fitted into 160x160 and kept in a small LRU cache.
 */
#include "covers.h"
#include "q2_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <list>
#include <map>

#include "lodepng.h"

namespace covers {
namespace {
std::string carts, art;
struct Entry { std::string name; Cover cover; };
std::list<Entry> cache; /* most recently used first */
enum { CACHE_LIMIT = 24, FILE_LIMIT = 8 << 20, SOURCE_LIMIT = 1024 };
std::map<std::string, float> requested;
Cover placeholder;

const uint32_t pico_palette[32] = {
    0x000000, 0x1d2b53, 0x7e2553, 0x008751, 0xab5236, 0x5f574f, 0xc2c3c7, 0xfff1e8,
    0xff004d, 0xffa300, 0xffec27, 0x00e436, 0x29adff, 0x83769c, 0xff77a8, 0xffccaa,
    0x291814, 0x111d35, 0x422136, 0x125359, 0x742f29, 0x49333b, 0xa28879, 0xf3ef7d,
    0xbe1250, 0xff6c24, 0xa8e72e, 0x00b543, 0x065ab5, 0x754665, 0xff6e59, 0xff9d81};

bool ends_with(const std::string &s, const char *tail) {
    size_t n = strlen(tail);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != tail[i]) return false;
    }
    return true;
}

/* A cart body 144x160 with a 128x128 label window at (8, 8). */
void cart_body(Cover &c) {
    c.w = 144;
    c.h = 160;
    c.px.assign(c.w * c.h, 0xff2a2d3au);
    for (unsigned y = 4; y < 140; ++y)
        for (unsigned x = 4; x < 140; ++x) c.px[y * c.w + x] = 0xff101014u;
    for (unsigned y = 146; y < 154; y += 3)
        for (unsigned x = 20; x < 124; ++x) c.px[y * c.w + x] = 0xff3d4152u;
}

void make_placeholder() {
    if (placeholder.w) return;
    cart_body(placeholder);
    /* A PICO-8 pink stripe across the empty label. */
    for (unsigned y = 64; y < 80; ++y)
        for (unsigned x = 8; x < 136; ++x) placeholder.px[y * placeholder.w + x] = 0xffff77a8u;
}

/* Fit RGBA bytes (w x h) into COVER_SIZE, averaging when shrinking. */
void fit(const unsigned char *rgba, unsigned w, unsigned h, bool opaque, Cover &out) {
    float scale = (float)COVER_SIZE / (float)(w > h ? w : h);
    if (scale > 1.0f && w <= COVER_SIZE && h <= COVER_SIZE) {
        /* Upscale only by whole steps, keeping pixel art crisp. */
        unsigned k = COVER_SIZE / (w > h ? w : h);
        scale = (float)(k ? k : 1);
    }
    unsigned dw = (unsigned)(w * scale + 0.5f), dh = (unsigned)(h * scale + 0.5f);
    if (!dw) dw = 1;
    if (!dh) dh = 1;
    out.w = dw;
    out.h = dh;
    out.px.assign(dw * dh, 0);
    for (unsigned y = 0; y < dh; ++y) {
        unsigned y0 = y * h / dh, y1 = (y + 1) * h / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (unsigned x = 0; x < dw; ++x) {
            unsigned x0 = x * w / dw, x1 = (x + 1) * w / dw;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
            for (unsigned sy = y0; sy < y1 && sy < h; ++sy)
                for (unsigned sx = x0; sx < x1 && sx < w; ++sx) {
                    const unsigned char *p = rgba + 4 * (sy * w + sx);
                    unsigned pa = opaque ? 255 : p[3];
                    r += p[0] * pa; g += p[1] * pa; b += p[2] * pa; a += pa; ++n;
                }
            if (!a) continue;
            out.px[y * dw + x] = ((a / n) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a);
        }
    }
}

bool load_png(const std::string &path, bool opaque, Cover &out) {
    struct stat st;
    if (stat(path.c_str(), &st) || !S_ISREG(st.st_mode) || st.st_size > FILE_LIMIT) return false;
    unsigned char *rgba = nullptr;
    unsigned w = 0, h = 0;
    if (lodepng_decode32_file(&rgba, &w, &h, path.c_str()) || !rgba) {
        free(rgba);
        return false;
    }
    bool ok = w && h && w <= SOURCE_LIMIT && h <= SOURCE_LIMIT;
    if (ok) fit(rgba, w, h, opaque, out);
    free(rgba);
    return ok;
}

int label_index(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'v') return c - 'a' + 10;
    return -1;
}

/* .p8 text carts: 128 rows of 128 colour digits after "__label__". */
bool load_label(const std::string &path, Cover &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (size_t)FILE_LIMIT) text.append(buf, n);
    fclose(f);
    size_t at = text.find("\n__label__");
    cart_body(out);
    bool drawn = false;
    if (at != std::string::npos) at = text.find('\n', at + 1);
    for (unsigned y = 0; y < 128 && at != std::string::npos; ++y) {
        size_t line = at + 1;
        at = text.find('\n', line);
        size_t end = at == std::string::npos ? text.size() : at;
        if (end > line && text[end - 1] == '\r') --end;
        if (end - line < 128) break;
        for (unsigned x = 0; x < 128; ++x) {
            int i = label_index(text[line + x]);
            if (i < 0) break;
            out.px[(y + 8) * out.w + x + 8] = 0xff000000u | pico_palette[i];
            drawn = true;
        }
    }
    if (!drawn) /* No label: the same pink stripe as a loading cover. */
        for (unsigned y = 64; y < 80; ++y)
            for (unsigned x = 8; x < 136; ++x) out.px[y * out.w + x] = 0xffff77a8u;
    return true;
}

Entry *find(const std::string &name) {
    for (auto it = cache.begin(); it != cache.end(); ++it)
        if (it->name == name) {
            cache.splice(cache.begin(), cache, it);
            return &cache.front();
        }
    return nullptr;
}
} // namespace

void set_dirs(const std::string &cart_dir, const std::string &art_dir) {
    carts = cart_dir;
    art = art_dir;
    clear();
}

bool load(const std::string &cart, Cover &out) {
    if (!art.empty() && load_png(art + "/" + cart + ".png", false, out)) return true;
    std::string path = carts + "/" + cart;
    /* A PNG cart hides its data in the low bits of every channel, alpha
     * included: draw it opaque. */
    if (ends_with(cart, ".p8.png")) return load_png(path, true, out);
    if (ends_with(cart, ".p8")) return load_label(path, out);
    return false;
}

void begin_frame() { requested.clear(); }

const Cover &get(const std::string &cart, float priority) {
    if (Entry *e = find(cart)) return e->cover;
    make_placeholder();
    if (priority < 0) priority = -priority;
    auto it = requested.find(cart);
    if (it == requested.end() || priority < it->second) requested[cart] = priority;
    return placeholder;
}

bool pending() { return !requested.empty(); }

bool pump(unsigned budget_ms) {
    unsigned start = q2_millis();
    bool loaded = false;
    while (!requested.empty()) {
        auto best = requested.begin();
        for (auto it = requested.begin(); it != requested.end(); ++it)
            if (it->second < best->second) best = it;
        std::string name = best->first;
        requested.erase(best);
        Entry entry;
        entry.name = name;
        if (!load(name, entry.cover)) {
            make_placeholder();
            entry.cover = placeholder;
        }
        cache.push_front(std::move(entry));
        if (cache.size() > CACHE_LIMIT) cache.pop_back();
        loaded = true;
        if ((unsigned)(q2_millis() - start) >= budget_ms) break;
    }
    return loaded;
}

void clear() {
    cache.clear();
    requested.clear();
}
}
