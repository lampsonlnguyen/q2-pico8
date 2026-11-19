/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native Shanling Q2 adapter to FAKE-08's Host interface.
 * Build this file INSIDE a FAKE-08 checkout with its C++17 engine, rather
 * than compiling it as part of the freestanding browser preview.
 * No SDL or desktop event loop is required.
 *
 * FAKE-08: https://github.com/jtothebell/fake-08 (MIT).
 */
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <algorithm>

#include "host.h"
#include "hostVmShared.h"
#include "q2_platform.h"

namespace {
constexpr const char *cart_dir = "/mnt/mmc/.pico8/carts";
constexpr const char *data_dir = "/mnt/mmc/.pico8/";
uint32_t last_frame = 0;
uint32_t target_ms = 33;
bool opened = false;
bool stopped = false;

bool ends_with(const std::string &s, const char *suffix) {
    std::string ext(suffix);
    return s.size() >= ext.size() &&
           s.compare(s.size()-ext.size(), ext.size(), ext) == 0;
}
} // namespace

Host::Host(int windowWidth, int windowHeight) {
    (void)windowWidth;
    (void)windowHeight;
    _cartDirectory = cart_dir;
    _logFilePrefix = data_dir;
#ifdef Q2_FAKE08_SMOKE
    _cartDirectory = "tests";
    _logFilePrefix = "/tmp/"; /* Host test uses an authored cartridge, not the SD card. */
#endif
    _customBiosLua = "cartpath = \"/mnt/mmc/.pico8/carts/\"\n"
                     "selectbtn = \"o\"\n"
                     "pausebtn = \"back\"\n";
}

void Host::oneTimeSetup(Audio *audio) {
    (void)audio;
    opened = q2_open() == 0;
    stopped = !opened;
    last_frame = q2_millis();
    mkdir("/mnt/mmc/.pico8/cdata", 0755);
}
void Host::oneTimeCleanup() {
    if(opened)q2_close();
    opened = false;
}
void Host::setTargetFps(int fps) {
    target_ms = fps > 0 ? (uint32_t)(1000/fps) : 33;
    if(!target_ms)target_ms = 1;
}
void Host::waitForTargetFps() {
    uint32_t now=q2_millis();
    uint32_t elapsed=now-last_frame;
    if(elapsed<target_ms)usleep((target_ms-elapsed)*1000u);
    last_frame=q2_millis();
}
bool Host::shouldQuit() { return stopped; }
bool Host::shouldRunMainLoop() { return !stopped; }
void Host::changeStretch() {}
void Host::forceStretch(StretchOption option) {(void)option;}
double Host::deltaTMs() { return static_cast<double>(q2_millis()-last_frame); }

InputState_t Host::scanInput() {
    const q2_input q=q2_read_input();
    if(q.quit)stopped=true;
    uint8_t down=0,held=0;
    if(q.pressed&Q2_LEFT)down|=P8_KEY_LEFT;
    if(q.pressed&Q2_RIGHT)down|=P8_KEY_RIGHT;
    if(q.pressed&Q2_UP)down|=P8_KEY_UP;
    if(q.pressed&Q2_DOWN)down|=P8_KEY_DOWN;
    if(q.pressed&Q2_O)down|=P8_KEY_O;
    if(q.pressed&Q2_PAUSE)down|=P8_KEY_PAUSE;
    if(q.pressed&Q2_X)down|=P8_KEY_X;
    if(q.buttons&Q2_LEFT)held|=P8_KEY_LEFT;
    if(q.buttons&Q2_RIGHT)held|=P8_KEY_RIGHT;
    if(q.buttons&Q2_UP)held|=P8_KEY_UP;
    if(q.buttons&Q2_DOWN)held|=P8_KEY_DOWN;
    if(q.buttons&Q2_O)held|=P8_KEY_O;
    if(q.buttons&Q2_X)held|=P8_KEY_X;
    held|=down;
    /* Preserve taps and wheel detents that began and ended within one frame. */
    return InputState_t {down,held,0,0,0,false,std::string()};
}
void Host::drawFrame(uint8_t *pixels,uint8_t *screenPaletteMap,uint8_t drawMode) {
    (void)drawMode;
    if(!opened||!pixels||!screenPaletteMap)return;
    uint32_t colors[144]={};
    Color *pal=GetPaletteColors();
    for(int i=0;i<144;i++)
        colors[i]=(uint32_t(pal[i].Red)<<16)|
                  (uint32_t(pal[i].Green)<<8)|uint32_t(pal[i].Blue);
    q2_present_fake08(pixels,screenPaletteMap,colors);
}
bool Host::shouldFillAudioBuff(){return false;}
void *Host::getAudioBufferPointer(){return nullptr;}
size_t Host::getAudioBufferSize(){return 0;}
void Host::playFilledAudioBuffer(){}

std::vector<std::string> Host::listcarts() {
    std::vector<std::string> carts;
    DIR *dir=opendir(_cartDirectory.c_str());
    if(!dir)return carts;
    struct dirent *entry;
    while((entry=readdir(dir))!=nullptr){
        const std::string name(entry->d_name);
        if(name.empty()||name[0]=='.')continue;
        if(ends_with(name,".p8")||ends_with(name,".p8.png"))
            carts.push_back(name);
    }
    closedir(dir);
    std::sort(carts.begin(),carts.end());
    return carts;
}
std::vector<std::string> Host::listdirs() {
    std::vector<std::string> names;
    DIR *dir=opendir(_cartDirectory.c_str());
    if(!dir)return names;
    struct dirent *entry;
    while((entry=readdir(dir))!=nullptr) {
        if(entry->d_name[0]=='.')continue;
        std::string name(entry->d_name);
        std::string path=_cartDirectory+"/"+name;
        struct stat st;
        if(stat(path.c_str(),&st)==0&&S_ISDIR(st.st_mode))names.push_back(name);
    }
    closedir(dir);
    return names;
}
void Host::overrideLogFilePrefix(const char *path){
    _logFilePrefix=path?path:data_dir;
}
const char *Host::logFilePrefix(){return _logFilePrefix.c_str();}
std::string Host::customBiosLua(){return _customBiosLua;}
std::string Host::getCartDirectory(){return _cartDirectory;}
void Host::setPlatformParams(int w,int h,uint32_t wf,uint32_t rf,uint32_t pf,
                             std::string log,std::string bios,std::string carts){
    (void)w;(void)h;(void)wf;(void)rf;(void)pf;
    _logFilePrefix=log;
    _customBiosLua=bios;
    _cartDirectory=carts;
}
