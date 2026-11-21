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
#include "cart_validate.h"

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
        if(input.quit||(input.pressed&Q2_PAUSE))break;
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
/* Show a cart-load error on the Q2 framebuffer and wait for Centre,
 * Back, or quit (60 s safety timeout so a dead input socket cannot
 * hang the app forever). Returns 2, like a cart that failed to load.
 * Presenting in the loop also keeps the framebuffer alive for the
 * firmware's input polling. */
static int show_cart_load_error(const char *name, const std::string &error) {
    uint8_t screen[8192];
    q2_draw_cart_error(screen, name, error.c_str());
    unsigned start = q2_millis();
    while (1) {
        q2_present(screen, palette);
        q2_input in = q2_read_input();
        if (in.quit || (in.pressed & (Q2_O | Q2_PAUSE))) break;
        if ((unsigned)(q2_millis() - start) > 60000u) break;
        usleep(16000);
    }
    return 2;
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
     * using a simulated framebuffer and input instead of a Q2 device.
     * audio.p8 plays a sustained sfx as well as drawing frames, so the
     * smoke test also verifies the synth produces non-silent audio. */
    const char *demo="audio.p8";
    for(unsigned i=0;demo[i]&&i+1<sizeof path;i++)path[i]=demo[i];
#else
    if(!pick_cart(path,sizeof path))return 0;
#endif
    int result=0;
    {
        Host host;
        Logger_Initialize(host.logFilePrefix());
        /* Vm creates and owns its Memory/Graphics/Input/Audio dependencies.
         * Passing stack allocations with null Graphics/Input makes upstream
         * _cleanupDeps true and causes an invalid free on shutdown. */
        Vm vm(&host,nullptr,nullptr,nullptr,nullptr);
        host.setUpPaletteColors();
        host.oneTimeSetup(nullptr);
        if(host.shouldQuit())result=1;
        else{
            vm.SetCartList(host.listcarts());
            /* FAKE-08's Cart opens files under Host::getCartDirectory().
             * Pass only the name chosen by the Q2 cartridge browser. */
            const char *filename=path;
            for(const char *p=path;*p;++p)if(*p=='/')filename=p+1;
            if(!vm.LoadCart(filename,false))result=2;
            else{
                /* Pre-validate the cart's Lua before starting the VM.
                 * LoadCart only parses the .p8 sections; a syntax error
                 * would otherwise surface on the first GameLoop tick,
                 * when upstream queues the BIOS menu and the error is
                 * lost (see cart_validate.h). A bad cart shows its
                 * name + error on screen and exits 2 instead. */
                std::string lua_error;
                if(!q2_cart_lua_ok(filename, host.getCartDirectory(), lua_error)){
                    result=show_cart_load_error(filename, lua_error);
                }else{
                    vm.vm_run();
                    host.setTargetFps(vm.GetTargetFps());
                    vm.GameLoop();
                }
            }
        }
        vm.CloseCart();
        host.oneTimeCleanup();
        /* Vm/Audio/Host destructors must run before Logger_Exit(),
         * which closes stderr through FAKE-08's Logger_Initialize. */
    }
    Logger_Exit();
    return result;
}
