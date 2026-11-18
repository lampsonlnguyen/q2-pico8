/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef Q2_FEMTO8_PLATFORM_H
#define Q2_FEMTO8_PLATFORM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { Q2_WIDTH=375, Q2_HEIGHT=320, P8_WIDTH=128, P8_HEIGHT=128 };
typedef uint32_t q2_pixel;
typedef struct { unsigned buttons; unsigned pressed; int wheel; unsigned quit; } q2_input;
enum { Q2_LEFT=1, Q2_RIGHT=2, Q2_UP=4, Q2_DOWN=8, Q2_O=16, Q2_X=32 };
/* Initialize framebuffer + Q2 Pod card input socket. Returns 0 on success. */
int q2_open(void);
/* Returns input snapshot. Button presses/rotation are latched until sampled. */
q2_input q2_read_input(void);
/* PICO-8 packed 4bpp framebuffer: 128x128, low nibble is left pixel. */
void q2_present(const uint8_t *packed_pixels, const uint32_t palette[16]);
/* FAKE-08 supports screen palette indices up to 143 (extended palette). */
void q2_present_fake08(const uint8_t *packed_pixels,
                       const uint8_t screen_map[16],
                       const uint32_t colors[144]);
unsigned q2_millis(void);
void q2_close(void);
#ifdef __cplusplus
}
#endif

#endif /* Q2_FEMTO8_PLATFORM_H */
