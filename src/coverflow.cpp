/* SPDX-License-Identifier: GPL-3.0-or-later
 * Cover Flow projection from Pocket Boy's cover.c
 * (github.com/SlayTheEarth/q2-boy, GPL-2.0-or-later): Q2 Pod Minimal's
 * Cover Flow uses a 90-degree turn, a 128 px first offset, 35 px side
 * spacing, 77 px of depth and a shared 64% horizon. Each screen column is
 * projected back onto the cover's actual rectangle, so non-square cart
 * images keep their proportions. */
#include "coverflow.h"
#include "q2_platform.h"
#include <math.h>

namespace coverflow {
namespace {
enum { WIDTH = Q2_WIDTH, HEIGHT = Q2_HEIGHT, REFLECTION = 46, COVER_TOP = 64, HORIZON = 64 };
constexpr float CAMERA_DEPTH = 256.0f;

struct Pose {
    float x, depth, cosine, sine;
    unsigned brightness;
    bool mirrored;
};

uint32_t composite(uint32_t color, uint32_t background, unsigned alpha) {
    unsigned rb = ((color & 0xff00ff) * alpha + (background & 0xff00ff) * (256 - alpha)) >> 8;
    unsigned g = ((color & 0x00ff00) * alpha + (background & 0x00ff00) * (256 - alpha)) >> 8;
    return (rb & 0xff00ff) | (g & 0xff00);
}

Pose pose(float distance) {
    float t = fabsf(distance);
    float turn = t < 1.0f ? t : 1.0f;
    float angle = turn * 1.57079632679f;
    float brightness = t <= 2.0f ? 256.0f - 64.0f * t : 128.0f;
    if (t > 3.0f) brightness *= (REACH - t) * 2.0f;
    return Pose{128.0f * turn + 35.0f * (t - turn), CAMERA_DEPTH + 77.0f * turn,
                turn == 1.0f ? 0.0f : cosf(angle), turn == 1.0f ? 1.0f : sinf(angle),
                (unsigned)(brightness + 0.5f), distance < 0};
}

bool column(const Cover &c, const Pose &p, int x, unsigned *source, int *top, int *height) {
    const float center = WIDTH / 2.0f;
    float dx = x + 0.5f - center;
    if (p.mirrored) dx = -dx;
    float denominator = CAMERA_DEPTH * p.cosine + dx * p.sine;
    if (denominator <= 0.0f) return false;
    float u = (dx * p.depth - CAMERA_DEPTH * p.x) / denominator;
    float width = (float)c.w;
    float sample = width * 0.5f + (p.mirrored ? -u : u);
    if (sample < 0.0f || sample >= width) return false;
    *source = (unsigned)sample;
    *height = (int)(c.h * CAMERA_DEPTH / (p.depth - u * p.sine) + 0.5f);
    int horizon = COVER_TOP + COVER_SIZE * HORIZON / 100;
    *top = horizon - *height * HORIZON / 100;
    return *height > 0;
}

void span(const Cover &c, const Pose &p, int *start, int *end) {
    float half = c.w * 0.5f;
    float left = CAMERA_DEPTH * (p.x - half * p.cosine) / (p.depth + half * p.sine);
    float right = CAMERA_DEPTH * (p.x + half * p.cosine) / (p.depth - half * p.sine);
    float center = WIDTH / 2.0f;
    *start = (int)(center + (p.mirrored ? -right : left));
    *end = (int)(center + (p.mirrored ? -left : right) + 0.5f);
    if (*start < 0) *start = 0;
    if (*end > WIDTH) *end = WIDTH;
}
} // namespace

int floor_y() { return COVER_TOP + COVER_SIZE; }

bool hit(const Cover &c, float distance, int x, int y) {
    if (fabsf(distance) >= REACH || !c.w) return false;
    unsigned source;
    int top, height;
    return column(c, pose(distance), x, &source, &top, &height) && y >= top && y < top + height;
}

void draw(uint32_t *canvas, const Cover &c, float distance, int offset_y) {
    if (fabsf(distance) >= REACH || !c.w || !c.h) return;
    Pose p = pose(distance);
    int start, end;
    span(c, p, &start, &end);
    for (int x = start; x < end; ++x) {
        unsigned source_x;
        int top, height;
        if (!column(c, p, x, &source_x, &top, &height)) continue;
        top += offset_y;
        unsigned step = (c.h << 16) / (unsigned)height;
        unsigned sample = 0;
        for (int y = 0; y < height; ++y, sample += step) {
            if ((unsigned)(top + y) >= HEIGHT) continue;
            uint32_t color = c.px[(sample >> 16) * c.w + source_x];
            unsigned alpha = (color >> 24) + (color >> 31);
            if (!alpha) continue;
            uint32_t *pixel = canvas + (top + y) * WIDTH + x;
            if (p.brightness != 256) color = composite(color, 0, p.brightness);
            *pixel = alpha == 256 ? color & 0xffffff : composite(color, *pixel, alpha);
        }
        int depth = height * REFLECTION / COVER_SIZE;
        int bottom = top + height;
        for (int y = 0; y < depth; ++y) {
            if ((unsigned)(bottom + y) >= HEIGHT) continue;
            unsigned source_y = c.h - 1 - (((unsigned)y * step) >> 16);
            uint32_t color = c.px[source_y * c.w + source_x];
            unsigned opacity = (color >> 24) + (color >> 31);
            unsigned fade = (depth - y) * 90 * p.brightness / (depth * 256);
            uint32_t *pixel = canvas + (bottom + y) * WIDTH + x;
            *pixel = composite(color, *pixel, fade * opacity / 256);
        }
    }
}
}
