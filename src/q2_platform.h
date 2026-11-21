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
enum { Q2_LEFT=1, Q2_RIGHT=2, Q2_UP=4, Q2_DOWN=8, Q2_O=16, Q2_X=32, Q2_PAUSE=64 };
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

/* Stereo ALSA audio, 22050 Hz interleaved s16 (FAKE-08's native rate).
 * Only built when Q2_GLIBC is defined: ALSA is resolved at runtime with
 * dlopen("libasound.so.2"), so the binary carries no link-time ALSA
 * dependency and the freestanding browser preview is unaffected.
 * q2_audio_open() returns 0 when the device opened and -1 when audio is
 * unavailable; the game then runs silent. q2_audio_write() queues one
 * video frame's worth of samples; it never blocks the game loop — a
 * full device buffer drops the chunk instead of stalling frame pacing.
 * All three are no-ops / -1 in the freestanding build. */
enum { Q2_AUDIO_RATE = 22050, Q2_AUDIO_CHANNELS = 2 };
int q2_audio_open(void);
void q2_audio_close(void);
int q2_audio_available(void);
void q2_audio_write(const int16_t *samples, unsigned frames);
#ifdef __cplusplus
}
#endif

#endif /* Q2_FEMTO8_PLATFORM_H */
