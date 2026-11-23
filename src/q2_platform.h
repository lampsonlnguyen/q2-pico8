/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef Q2_FEMTO8_PLATFORM_H
#define Q2_FEMTO8_PLATFORM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { Q2_WIDTH=375, Q2_HEIGHT=320, P8_WIDTH=128, P8_HEIGHT=128 };
typedef uint32_t q2_pixel;
/* touch: 1 while a finger is down, at (touch_x, touch_y) in the 375x320
 * view; touch_began/touch_ended latch a down/up since the last read, and
 * touch_start_x/y is where the current or last touch began. */
typedef struct {
    unsigned buttons; unsigned pressed; unsigned quit;
    int touch, touch_x, touch_y, touch_start_x, touch_start_y;
    unsigned touch_began, touch_ended;
} q2_input;
enum { Q2_LEFT=1, Q2_RIGHT=2, Q2_UP=4, Q2_DOWN=8, Q2_O=16, Q2_X=32, Q2_PAUSE=64 };
/* Initialize framebuffer + Q2 Pod card input socket. Returns 0 on success. */
int q2_open(void);
/* Returns input snapshot. Button presses are latched until sampled. */
q2_input q2_read_input(void);
/* PICO-8 packed 4bpp framebuffer: 128x128, low nibble is left pixel. */
void q2_present(const uint8_t *packed_pixels, const uint32_t palette[16]);
/* FAKE-08 supports screen palette indices up to 143 (extended palette). */
void q2_present_fake08(const uint8_t *packed_pixels,
                       const uint8_t screen_map[16],
                       const uint32_t colors[144]);
/* Native-resolution UI: a 375x320 0xRRGGBB canvas, row-major
 * (canvas[y*Q2_WIDTH+x]), presented with the same rotation and page flip
 * as the game picture. The library and Get Games screens draw here. */
uint32_t *q2_canvas(void);
void q2_present_canvas(void);
unsigned q2_millis(void);
void q2_close(void);
/* Startup diagnostics that survive a hang or a forced power-off: appends
 * "stage N: what" to /mnt/mmc/.pico8/startup.txt (fsync'd) and draws band N
 * (16 px at y = 8 + 20 N) in rgb straight onto the visible framebuffer page,
 * opening it if q2_open() has not. Later frames replace the bands, so bands
 * left on screen show how far startup got. No-op outside Q2_GLIBC builds. */
void q2_debug_stage(unsigned stage, uint32_t rgb, const char *what);
/* Q2 Pod starts an audio=1 game with the output device (plughw:0,0,
 * plug:bluealsa or usb) and, off the jack, its volume (NN soft, hNN shown
 * only) as arguments: q2_audio_open() uses them. */
void q2_set_args(int argc, char **argv);
/* Gameplay maps the four outer buttons to directions and Centre to X.
 * Menus use Centre to confirm and Return to go back. */
void q2_set_game_mode(int on);
void q2_resume_display(void);
void q2_set_audio_route(const char *device, const char *volume);

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
