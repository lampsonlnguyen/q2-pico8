/* SPDX-License-Identifier: GPL-3.0-or-later
 * SD-card cartridge browser hardware preview. Runtime is NOT linked yet.
 */
#include "q2_platform.h"
#include "cart_browser.h"
static q2_cart_list carts;
static uint8_t framebuffer[8192];
static const uint32_t palette[16]={
 0x000000,0x1d2b53,0x7e2553,0x008751,0xab5236,0x5f574f,0xc2c3c7,0xfff1e8,
 0xff004d,0xffa300,0xffec27,0x00e436,0x29adff,0x83769c,0xff77a8,0xffccaa
};
int q2_main(void){
    if(q2_open())return 1;
    q2_find_carts(&carts);
    unsigned redraw=1,last=q2_millis(),pending=0;
    while(1){
        q2_input input=q2_read_input();
        if(input.quit || (input.pressed&Q2_X))break;
        if(input.wheel!=0 && carts.count){
            int step=input.wheel>0?1:-1;
            if(step>0)carts.selected=(carts.selected+1)%carts.count;
            else carts.selected=(carts.selected+carts.count-1)%carts.count;
            pending=0;redraw=1;
        }
        if(input.pressed&Q2_O){
            if(carts.count){pending=1;redraw=1;}
            else {q2_find_carts(&carts);redraw=1;}
        }
        if(redraw){
            q2_draw_cart_browser(framebuffer,&carts,(int)pending);
            q2_present(framebuffer,palette);
            redraw=0;
        }
        if((unsigned)(q2_millis()-last)>10000){
            /* Refresh folder periodically, without installing each game. */
            unsigned prev=carts.count;
            q2_find_carts(&carts);
            if(carts.count!=prev){redraw=1;pending=0;}
            last=q2_millis();
        }
    }
    q2_close();return 0;
}
