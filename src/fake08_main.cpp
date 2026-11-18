/* SPDX-License-Identifier: GPL-3.0-or-later
 * Experimental FAKE-08 entry point for Q2 Pod. Unlike cart_browser_probe.c,
 * this actually loads a .p8/.p8.png cart with the upstream FAKE-08 VM.
 * Build only when a C++17 MIPS toolchain and Q2 glibc/sysroot are available.
 */
#include <stdint.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "q2_platform.h"
#include "cart_browser.h"

#include "host.h"
#include "vm.h"
#include "PicoRam.h"
#include "Audio.h"
#include "logger.h"

static const uint32_t palette[16]={
    0x000000,0x1d2b53,0x7e2553,0x008751,
    0xab5236,0x5f574f,0xc2c3c7,0xfff1e8,
    0xff004d,0xffa300,0xffec27,0x00e436,
    0x29adff,0x83769c,0xff77a8,0xffccaa
};
static q2_cart_list carts;
static uint8_t browser_image[8192];
static int pick_cart(char *path,unsigned cap){
    if(q2_open())return 0;
    q2_find_carts(&carts);
    unsigned dirty=1,last=0;
    int chosen=0;
    while(1){
        q2_input input=q2_read_input();
        if(input.quit||(input.pressed&Q2_X))break;
        if(input.wheel&&carts.count){
            if(input.wheel>0)carts.selected=(carts.selected+1u)%carts.count;
            else carts.selected=(carts.selected+carts.count-1u)%carts.count;
            dirty=1;
        }
        if(input.pressed&Q2_O){
            if(carts.count){
                chosen=q2_cart_path(&carts,path,cap);
                break;
            }
            q2_find_carts(&carts);
            dirty=1;
        }
        if(dirty){
            q2_draw_cart_browser(browser_image,&carts,0);
            q2_present(browser_image,palette);
            dirty=0;
        }
        unsigned now=q2_millis();
        if((unsigned)(now-last)>10000){
            unsigned previous=carts.count;
            q2_find_carts(&carts);
            if(carts.count!=previous)dirty=1;
            last=now;
        }
        usleep(16000);
    }
    q2_close();
    return chosen;
}
/* q2_platform.c supplies the native MIPS __start entry and calls q2_main.
 * Do not link cart_browser_probe.c alongside this file. */
#ifdef Q2_GLIBC
int main(void) {
#else
extern "C" int q2_main(void) {
#endif
    char path[160]={};
#ifdef Q2_FAKE08_SMOKE
    /* Run a locally authored test cartridge against the actual FAKE-08 VM,
     * using a simulated framebuffer and input instead of a Q2 device. */
    const char *demo="tests/demo.p8";
    for(unsigned i=0;demo[i]&&i+1<sizeof path;i++)path[i]=demo[i];
#else
    if(!pick_cart(path,sizeof path))return 0;
#endif
    Host host;
    PicoRam memory;
    memory.Reset();
    Audio audio(&memory);
    Logger_Initialize(host.logFilePrefix());
    Vm vm(&host,&memory,nullptr,nullptr,&audio);
    host.setUpPaletteColors();
    host.oneTimeSetup(&audio);
    if(host.shouldQuit()){Logger_Exit();return 1;}
    host.setTargetFps(30);
    vm.SetCartList(host.listcarts());
    vm.LoadCart(path,true);
    vm.vm_run();
    vm.GameLoop();
    vm.CloseCart();
    host.oneTimeCleanup();
    Logger_Exit();
    return 0;
}
