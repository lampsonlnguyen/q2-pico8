/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef Q2_CART_BROWSER_H
#define Q2_CART_BROWSER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define Q2_CART_MAX 64
#define Q2_CART_NAME_MAX 60
#define Q2_CART_DIR "/mnt/mmc/.pico8/carts"
typedef struct {
    char names[Q2_CART_MAX][Q2_CART_NAME_MAX];
    unsigned count;
    unsigned selected;
} q2_cart_list;
/* Scan SD-card directory; returns number of supported .p8/.p8.png names. */
unsigned q2_find_carts(q2_cart_list *list);
/* Selected absolute filename, or 0 if selection invalid. */
int q2_cart_path(const q2_cart_list *list, char *out, unsigned cap);
/* Draw 128x128 4bpp browser screen into PICO-8-compatible pixel buffer. */
void q2_draw_cart_browser(uint8_t *screen,const q2_cart_list *list,int pending);
/* Draw a 128x128 4bpp "cart failed to load" screen: cart name plus the
 * (wrapped) load/syntax error, with CENTER/BACK to exit at the foot.
 * The caller presents it with q2_present() and the normal palette. */
void q2_draw_cart_error(uint8_t *screen,const char *name,const char *error);
#ifdef __cplusplus
}
#endif

#endif /* Q2_CART_BROWSER_H */
