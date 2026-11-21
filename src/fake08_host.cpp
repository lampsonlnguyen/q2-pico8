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
#include "cart_validate.h"

/* Upstream Cart parser + z8lua compiler, for q2_cart_lua_ok() below. */
#include "cart.h"
#include "lua.h"
#include "lauxlib.h"

namespace {
constexpr const char *cart_dir = "/mnt/mmc/.pico8/carts";
constexpr const char *data_dir = "/mnt/mmc/.pico8/";
/* FAKE-08's synth runs at 22050 Hz stereo. One video frame's worth of
 * audio is 22050/fps frames: 735 at 30 FPS, 367/368 alternating at
 * 60 FPS. The buffer holds 2048 frames, so any fps >= 11 fits; lower
 * rates are clamped (audio runs slightly slow rather than overflowing).
 * Audio is filled every video frame whether or not ALSA opened: that
 * keeps the synth clock tied to the frame clock (as in FAKE-08's SDL
 * hosts, whose callback always runs) and keeps the path testable in
 * the native smoke test, where there is no ALSA device. When ALSA is
 * missing on the Q2, playFilledAudioBuffer discards the samples and
 * the game runs silent. */
constexpr unsigned audio_rate = 22050;
constexpr unsigned audio_buffer_frames = 2048;
uint32_t audio_buffer[audio_buffer_frames];
double audio_credit = 0;
unsigned audio_frames = 0;
int audio_fps = 60;
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
    if (opened) q2_audio_open(); /* -1 when ALSA is missing: silent mode. */
    audio_credit = 0;
    audio_frames = 0;
    last_frame = q2_millis();
    mkdir("/mnt/mmc/.pico8/cdata", 0755);
}
void Host::oneTimeCleanup() {
    q2_audio_close();
    if(opened)q2_close();
    opened = false;
}
void Host::setTargetFps(int fps) {
    target_ms = fps > 0 ? (uint32_t)(1000/fps) : 33;
    if(!target_ms)target_ms = 1;
    audio_fps = (fps > 0 && fps <= 240) ? fps : 60;
    audio_credit = 0;
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
bool Host::shouldFillAudioBuff(){
    if(!opened||stopped)return false;
    /* One frame's worth of 22050 Hz samples, fractional part carried
     * forward so 60 FPS alternates 367/368 frames and never drifts. */
    audio_credit += (double)audio_rate / (double)audio_fps;
    unsigned frames = (unsigned)audio_credit;
    audio_credit -= (double)frames;
    if(frames > audio_buffer_frames)frames = audio_buffer_frames;
    audio_frames = frames;
    return audio_frames > 0;
}
void *Host::getAudioBufferPointer(){return audio_buffer;}
size_t Host::getAudioBufferSize(){return audio_frames;}
void Host::playFilledAudioBuffer(){
    if(!audio_frames)return;
    /* FillAudioBuffer writes one uint32 per frame (the same s16 sample
     * in both halves). On little-endian MIPS that is already interleaved
     * stereo s16, so the buffer can be handed to ALSA as-is. */
    q2_audio_write(reinterpret_cast<const int16_t *>(audio_buffer), audio_frames);
    audio_frames = 0;
}

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

/* See cart_validate.h. The Cart probe re-parses the file exactly as
 * Vm::LoadCart will, so a cart that passes here will parse there too.
 * luaL_loadstring compiles with z8lua's own parser — the same one
 * __z8_load_code uses inside the VM — so PICO-8 syntax extensions
 * (e.g. +=, //) are accepted exactly as the VM accepts them, and a
 * syntax error produces the VM's own message. No libraries are opened:
 * this only compiles, it never runs the cart. */
bool q2_cart_lua_ok(const std::string &filename,
                    const std::string &cart_dir,
                    std::string &error_out) {
    Cart probe(filename, cart_dir);
    if (!probe.LoadError.empty()) {
        error_out = probe.LoadError;
        return false;
    }
    if (probe.LuaString.empty()) {
        error_out = "Cart file has no Lua code";
        return false;
    }
    lua_State *L = luaL_newstate();
    if (!L) {
        error_out = "Out of memory checking cart Lua";
        return false;
    }
    int status = luaL_loadstring(L, probe.LuaString.c_str());
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        error_out = msg ? msg : "syntax error";
        lua_close(L);
        return false;
    }
    lua_close(L);
    return true;
}
