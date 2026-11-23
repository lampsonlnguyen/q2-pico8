/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native Shanling Q2 adapter to FAKE-08's Host interface.
 * Build this file INSIDE a FAKE-08 checkout with its C++17 engine, rather
 * than compiling it as part of the freestanding browser preview.
 * No SDL or desktop event loop is required.
 *
 * FAKE-08: https://github.com/jtothebell/fake-08 (MIT).
 */
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <algorithm>

#include "host.h"
#include "hostVmShared.h"
#include "vm.h"
#include "q2_platform.h"
#include "cart_validate.h"
#include "fake08_session.h"
#include "app.h"
#include "ui.h"
#include "q2_input.h"

/* Upstream Cart parser + z8lua compiler, for q2_cart_lua_ok() below. */
#include "cart.h"
#include "lua.h"
#include "lauxlib.h"

Vm *q2_session_vm;
int q2_session_end;

namespace {
/* FAKE-08's pause menu exits load its own BIOS carts; the build relabels
 * them (tools/fake08-overlay.sh) and leaving_cart() below turns them into
 * "back to the library" and "leave the app". */
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

bool game_muted = false;
bool music_mode = false;
bool audio_bridge = false;
bool audio_initialized = false;

#if defined(Q2_GLIBC) && !defined(Q2_HOST_SIM) && !defined(Q2_FAKE08_SMOKE)
constexpr char audio_path[] = "/tmp/q2-pico8.audio";
constexpr char view_path[] = "/tmp/q2-pico8.view";
char audio_device[16], audio_volume[8];

bool switch_audio(bool music) {
    if (music_mode == music) return true;
    if (music) q2_audio_close();
    if (!app::write_lines(audio_path, {music ? "music" : "game"})) {
        if (music) q2_audio_open();
        return false;
    }
    const unsigned started = q2_millis();
    do {
        if (music) {
            if (access(audio_path, F_OK) != 0 && errno == ENOENT) {
                music_mode = true;
                return true;
            }
        } else {
            auto reply = app::read_lines(audio_path, 1);
            if (!reply.empty() && reply[0] != "game") {
                audio_volume[0] = 0;
                int fields = sscanf(reply[0].c_str(), "%15s %7s", audio_device, audio_volume);
                if (fields > 0) {
                    q2_set_audio_route(audio_device, audio_volume);
                    if (!q2_audio_open()) {
                        music_mode = false;
                        return true;
                    }
                }
                break;
            }
        }
        if (q2_read_input().quit) break;
        usleep(20000);
    } while (q2_millis() - started < 5000u);
    if (music) q2_audio_open();
    else app::write_lines(audio_path, {"music"});
    return false;
}

bool now_playing() {
    bool restore_game = !music_mode;
    if (!switch_audio(true)) return false;
    bool opened_view = false;
    if (app::write_lines(view_path, {"now"})) {
        unsigned started = q2_millis();
        while (true) {
            auto view = app::read_lines(view_path, 1);
            if (view.empty()) break;
            if (view[0] == "open") opened_view = true;
            if ((!opened_view && q2_millis() - started >= 5000u) || q2_read_input().quit) {
                unlink(view_path);
                break;
            }
            usleep(20000);
        }
        q2_resume_display();
    }
    bool restored = !restore_game || switch_audio(false);
    return opened_view && restored;
}
#else
bool switch_audio(bool) { return false; }
bool now_playing() { return false; }
#endif

enum class MenuPage { Main, Display, Audio };

bool game_menu() {
    q2_set_game_mode(0);
    MenuPage page = MenuPage::Main;
    const char *status = "";
    int focus = 0;
    bool cart_menu = false;
    bool dirty = true;
    constexpr int left = 38, top = 53, width = ui::W - 2 * left, row = 36;
    while (true) {
        const q2_input in = q2_read_input();
        if (in.quit) {
            stopped = true;
            q2_session_end = Q2_END_QUIT;
            break;
        }
        bool main = page == MenuPage::Main;
        bool display = page == MenuPage::Display;
        const int count = main ? 7 : display ? 4 : 3;
        if (in.pressed & Q2_PAUSE) {
            if (main) break;
            page = MenuPage::Main;
            focus = 0;
            status = "";
            dirty = true;
            continue;
        }
        int move = !!(in.pressed & (Q2_RIGHT | Q2_X)) - !!(in.pressed & Q2_LEFT);
        if (move) {
            focus = (focus + move + count) % count;
            dirty = true;
        }
        int tap = -1;
        if (in.touch_ended && in.touch_start_x >= left && in.touch_start_x < left + width &&
            in.touch_x >= left && in.touch_x < left + width &&
            in.touch_start_y >= top && in.touch_y >= top) {
            int y = in.touch_y - top;
            if (y / row < count && y % row < row - 4 &&
                (in.touch_start_y - top) / row == y / row) tap = y / row;
        }
        if (tap >= 0 || (in.pressed & Q2_O)) {
            if (tap >= 0) focus = tap;
            status = "";
            if (main) {
                if (focus == 0) break;
                if (focus == 1) {
                    if (audio_bridge && !now_playing()) status = "Could not open Q2 Now Playing";
                } else if (focus == 2 || focus == 3) {
                    page = focus == 2 ? MenuPage::Display : MenuPage::Audio;
                    focus = 0;
                } else if (focus == 4 || focus == 5) {
                    q2_session_end = focus == 4 ? Q2_END_LIBRARY : Q2_END_QUIT;
                    break;
                } else {
                    cart_menu = true;
                    break;
                }
            } else if (focus == count - 1) {
                page = MenuPage::Main;
                focus = 0;
            } else if (display) {
                if (focus == 0) q2_overlay_set_visible(!q2_overlay_visible());
                if (focus == 1) q2_input_set_centre(q2_input_centre() == Q2_O ? Q2_X : Q2_O);
                if (focus == 2) q2_input_set_game_wheel(!q2_input_game_wheel());
                q2_overlay_mark_changed();
            } else if (focus == 0) {
                game_muted = !game_muted;
            } else if (audio_bridge && !switch_audio(!music_mode)) {
                status = "Could not switch audio";
            }
            dirty = true;
            continue;
        }
        if (dirty) {
            const char *const main_labels[] = {"Resume", "Q2 Now Playing", "Display and controls",
                                                "Audio", "ROM Library", "Exit to Q2 Pod", "Cartridge menu"};
            const char *const display_labels[] = {
                q2_overlay_visible() ? "Touch controls: Shown" : "Touch controls: Hidden",
                q2_input_centre() == Q2_O ? "Centre: O / Touch: X" : "Centre: X / Touch: O",
                q2_input_game_wheel() ? "In-game Clickwheel: On" : "In-game Clickwheel: Off", "Back"};
            const char *const audio_labels[] = {
                game_muted ? "Game sound: Muted" : "Game sound: On",
                music_mode ? "Audio source: Q2 Music" : "Audio source: Game", "Back"};
            const auto labels = main ? main_labels : display ? display_labels : audio_labels;
            ui::clear(ui::BLACK);
            ui::caption(16, main ? "Menu" : display ? "Display and controls" : "Audio",
                        ui::WHITE, ui::LARGE);
            for (int i = 0; i < count; ++i) {
                bool enabled = audio_bridge || (main ? i != 1 : display || i != 1);
                ui::button(left, top + i * row, width, row - 4, labels[i], enabled, i == focus);
            }
            if (*status) ui::caption(303, status, ui::WHITE, ui::SMALL);
            else if (display) {
                ui::caption(218, "Return / Play: Up / Down", ui::GRAY, ui::SMALL);
                ui::caption(242, "Previous / Next: Left / Right", ui::GRAY, ui::SMALL);
            }
            q2_present_canvas();
            dirty = false;
        }
        usleep(16000);
    }
    q2_set_game_mode(1);
    last_frame = q2_millis();
    return cart_menu;
}


bool ends_with(const std::string &s, const char *suffix) {
    std::string ext(suffix);
    return s.size() >= ext.size() &&
           s.compare(s.size()-ext.size(), ext.size(), ext) == 0;
}
} // namespace

void q2_configure_audio(int argc) {
#if defined(Q2_GLIBC) && !defined(Q2_HOST_SIM) && !defined(Q2_FAKE08_SMOKE)
    bool audio2 = false, gameview = false;
    for (const auto &line : app::read_lines("/tmp/q2-addon.caps", 16)) {
        std::string words = " " + line + " ";
        audio2 |= words.find(" audio2 ") != std::string::npos;
        gameview |= words.find(" gameview ") != std::string::npos;
    }
    const std::string manifest = app::data_dir() + "/q2app";
    auto lines = app::read_lines(manifest, 32);
    for (auto &line : lines) {
        if (line != "audio=1" && line != "audio=2") continue;
        audio_bridge = audio2 && gameview && argc == 1 && line == "audio=2";
        const char *wanted = audio2 && gameview ? "audio=2" : "audio=1";
        if (line != wanted) {
            // The current launch still uses the manifest Q2 Pod already read.
            music_mode = argc == 1 && line == "audio=2";
            line = wanted;
            app::write_lines(manifest, lines);
        }
        break;
    }
    music_mode = music_mode || audio_bridge;
#else
    (void)argc;
#endif
}

Host::Host(int windowWidth, int windowHeight) {
    (void)windowWidth;
    (void)windowHeight;
    _cartDirectory = app::cart_dir();
    _logFilePrefix = app::data_dir() + "/";
#ifdef Q2_FAKE08_SMOKE
    _cartDirectory = "tests";
    _logFilePrefix = "/tmp/"; /* Host test uses an authored cartridge, not the SD card. */
#endif
    _customBiosLua = "cartpath = \"" + app::cart_dir() + "/\"\n"
                     "selectbtn = \"o\"\n"
                     "pausebtn = \"back\"\n";
}

/* The platform stays open for the whole session (library and carts); the
 * audio device opens with the first cart and stays open between carts. */
void Host::oneTimeSetup(Audio *audio) {
    (void)audio;
    opened = true;
    stopped = false;
    q2_session_end = Q2_END_NONE;
    if (audio_bridge && !audio_initialized) switch_audio(false);
    else if (!music_mode && !q2_audio_available()) q2_audio_open();
    audio_initialized = true;
    audio_credit = 0;
    audio_frames = 0;
    last_frame = q2_millis();
    mkdir((app::data_dir() + "/cdata").c_str(), 0755);
}
void Host::oneTimeCleanup() {
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
static bool leaving_cart() {
    if(!q2_session_vm||q2_session_end!=Q2_END_NONE)return q2_session_end!=Q2_END_NONE;
    std::string name=q2_session_vm->CurrentCartFilename();
    if(name=="__FAKE08-DEFAULT.p8")q2_session_end=Q2_END_LIBRARY;
    else if(name=="__FAKE08-SETTINGS.p8")q2_session_end=Q2_END_QUIT;
    return q2_session_end!=Q2_END_NONE;
}
bool Host::shouldQuit() { return stopped; }
bool Host::shouldRunMainLoop() { return !stopped && !leaving_cart(); }
void Host::changeStretch() {}
void Host::forceStretch(StretchOption option) {(void)option;}
double Host::deltaTMs() { return static_cast<double>(q2_millis()-last_frame); }

InputState_t Host::scanInput() {
    const q2_input q=q2_read_input();
    if(q.quit){stopped=true;q2_session_end=Q2_END_QUIT;}
    if (!q.quit && (q.pressed & Q2_PAUSE)) {
        uint8_t pause = game_menu() ? P8_KEY_PAUSE : 0;
        return InputState_t {pause,pause,0,0,0,false,std::string()};
    }
    uint8_t down=0,held=0;
    if(q.pressed&Q2_LEFT)down|=P8_KEY_LEFT;
    if(q.pressed&Q2_RIGHT)down|=P8_KEY_RIGHT;
    if(q.pressed&Q2_UP)down|=P8_KEY_UP;
    if(q.pressed&Q2_DOWN)down|=P8_KEY_DOWN;
    if(q.pressed&Q2_O)down|=P8_KEY_O;
    if(q.pressed&Q2_X)down|=P8_KEY_X;
    if(q.buttons&Q2_LEFT)held|=P8_KEY_LEFT;
    if(q.buttons&Q2_RIGHT)held|=P8_KEY_RIGHT;
    if(q.buttons&Q2_UP)held|=P8_KEY_UP;
    if(q.buttons&Q2_DOWN)held|=P8_KEY_DOWN;
    if(q.buttons&Q2_O)held|=P8_KEY_O;
    if(q.buttons&Q2_X)held|=P8_KEY_X;
    held|=down;
    /* Preserve button taps that began and ended within one frame. */
    return InputState_t {down,held,static_cast<int16_t>(q.mouse_x),static_cast<int16_t>(q.mouse_y),
                         static_cast<uint8_t>(q.mouse_left),false,std::string()};
}
void Host::drawFrame(uint8_t *pixels,uint8_t *screenPaletteMap,uint8_t drawMode) {
    (void)drawMode;
    if(!opened||!pixels||!screenPaletteMap||leaving_cart())return;
    uint32_t colors[144]={};
    Color *pal=GetPaletteColors();
    for(int i=0;i<144;i++)
        colors[i]=(uint32_t(pal[i].Red)<<16)|
                  (uint32_t(pal[i].Green)<<8)|uint32_t(pal[i].Blue);
    q2_present_fake08(pixels,screenPaletteMap,colors);
}
bool Host::shouldFillAudioBuff(){
    if(!opened||stopped||leaving_cart())return false;
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
    if (!game_muted && !music_mode)
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
    _logFilePrefix=path?path:app::data_dir()+"/";
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
