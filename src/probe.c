/* SPDX-License-Identifier: GPL-3.0-or-later
 * Hardware bring-up diagnostic: verifies correct framebuffer, wheel, buttons.
 * This is NOT a PICO-8 interpreter yet.
 */
#include "q2_platform.h"
static uint8_t framebuffer[128*128/2];
static const uint32_t palette[16]={
  0x000000,0x1d2b53,0x7e2553,0x008751,0xab5236,0x5f574f,0xc2c3c7,0xfff1e8,
  0xff004d,0xffa300,0xffec27,0x00e436,0x29adff,0x83769c,0xff77a8,0xffccaa
};
static unsigned x=64,anim;
static void draw(void) {
    for(unsigned y=0;y<128;++y)for(unsigned k=0;k<64;++k) {
        unsigned left=((y/16)+(k/8))&15, right=((y/16)+((2*k+1)/16))&15;
        framebuffer[y*64+k]=(uint8_t)(left|(right<<4));
    }
    for(unsigned y=48;y<80;++y)for(unsigned a=0;a<20;++a){
        unsigned xx=(x+a)%128,off=y*64+xx/2;
        if(xx&1)framebuffer[off]=(framebuffer[off]&15)|((anim&15)<<4);
        else framebuffer[off]=(framebuffer[off]&240)|(anim&15);
    }
}
int q2_main(void) {
    if(q2_open())return 1;
    unsigned last=q2_millis(),frame=0;
    while(1) {
        q2_input input=q2_read_input();
        if(input.quit)break;
        if(input.wheel>0)x=(x+3)%128;
        if(input.wheel<0)x=(x+125)%128;
        if(input.pressed&Q2_O)anim=(anim+1)&15;
        unsigned now=q2_millis();
        if((unsigned)(now-last)>=33){
            last=now;draw();q2_present(framebuffer,palette);
            if(++frame>18000)break; /* failsafe: ~10 min */
        }
    }
    q2_close();return 0;
}
