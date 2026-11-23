/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef Q2_COVERFLOW_H
#define Q2_COVERFLOW_H
#include "covers.h"

namespace coverflow {
/* Covers farther than this from the centre are not drawn. */
constexpr float REACH = 3.5f;
/* Horizontal drag that moves the shelf by one cover. */
constexpr int DRAG_PIXELS = 90;
/* distance: position relative to the centred cover (0 = centre). */
void draw(uint32_t *canvas, const Cover &cover, float distance, int offset_y = 0);
bool hit(const Cover &cover, float distance, int x, int y);
/* Bottom edge of the centred cover's picture (for placing captions). */
int floor_y();
}
#endif
