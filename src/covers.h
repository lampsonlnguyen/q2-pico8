/* SPDX-License-Identifier: GPL-3.0-or-later
 * Cart artwork for the Cover Flow library. */
#ifndef Q2_COVERS_H
#define Q2_COVERS_H
#include <stdint.h>
#include <string>
#include <vector>

enum { COVER_SIZE = 160 };

/* 0xAARRGGBB pixels, fitted inside COVER_SIZE x COVER_SIZE. */
struct Cover {
    unsigned w = 0, h = 0;
    std::vector<uint32_t> px;
};

namespace covers {
/* cart_dir holds the carts; art_dir holds optional "<cart file>.png"
 * artwork (Get Games saves itch.io covers there). */
void set_dirs(const std::string &cart_dir, const std::string &art_dir);
/* Call before a frame's get() calls: forgets the previous frame's requests. */
void begin_frame();
/* The cart's cover, or a placeholder while it loads. Lower priority
 * values load first (pass the distance from the centre). */
const Cover &get(const std::string &cart, float priority);
/* Loads requested covers for up to budget_ms; returns true if any loaded. */
bool pump(unsigned budget_ms);
bool pending();
void clear();
/* Exposed for tests: decode a cart's artwork as get() would. */
bool load(const std::string &cart, Cover &out);
}
#endif
