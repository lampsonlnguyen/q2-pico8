/* SPDX-License-Identifier: GPL-3.0-or-later
 * Integration shim to compile INSIDE an upstream femto8 checkout.
 * This file is not linked into the standalone hardware probe yet.
 * Keep upstream interpreter and parser unchanged.
 */
#include "q2_platform.h"
#include "p8_emu.h"
#include "p8_input.h"

/* The first port keeps the basic Pico-8 palette, with no display-palette
 * remapping. Full palette-bank and transparency support is a later milestone. */
static const uint32_t default_palette[16] = {
  0x000000,0x1d2b53,0x7e2553,0x008751,
  0xab5236,0x5f574f,0xc2c3c7,0xfff1e8,
  0xff004d,0xffa300,0xffec27,0x00e436,
  0x29adff,0x83769c,0xff77a8,0xffccaa
};

void q2_femto8_video(void) {
    /* PICO-8 stores 2 pixels per byte at 0x6000 in 64 KB RAM. */
    if(m_memory)
        q2_present(m_memory + MEMORY_SCREEN,default_palette);
}

/* Call once per emulator frame, after input reset. Wheel produces directional
 * pulses because Q2 Pod exposes relative wheel rotation, not a held D-pad. */
void q2_femto8_input(void) {
    q2_input input=q2_read_input();
    unsigned buttons=0;
    if(input.pressed&Q2_LEFT)buttons|=BUTTON_MASK_LEFT;
    if(input.pressed&Q2_RIGHT)buttons|=BUTTON_MASK_RIGHT;
    if(input.buttons&Q2_O)buttons|=BUTTON_MASK_ACTION1;
    if(input.pressed&Q2_O)buttons|=BUTTON_MASK_ACTION1;
    if(input.pressed&Q2_X)buttons|=BUTTON_MASK_PAUSE;
    m_buttonsp[0]=(uint16_t)(buttons&~m_buttons[0]);
    m_buttons[0]=(uint16_t)buttons;
    if(m_memory)m_memory[MEMORY_BUTTON_STATE]=(uint8_t)buttons;
    if(input.quit)p8_quit();
}
