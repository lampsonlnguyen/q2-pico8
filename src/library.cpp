/* SPDX-License-Identifier: GPL-3.0-or-later
 * Cover Flow cart library, after Pocket Boy's ROM shelf
 * (github.com/SlayTheEarth/q2-boy):
 *   Previous / Next / swipe   browse
 *   Centre or tap the centre cover    play
 *   tap a side cover                  select it
 *   Play/Pause or tap the dots        menu (Get Games, sort, refresh, exit)
 *   Return                            leave the app
 */
#include "library.h"

#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#include "app.h"
#include "coverflow.h"
#include "covers.h"
#include "getgames.h"
#include "logger.h"
#include "q2_input.h"
#include "ui.h"

namespace {
enum { FRAME_MS = 33, HEADER = 44, TITLE_Y = 234, SUB_Y = 266, HINT_Y = 296,
       MENU_W = 260, MENU_ROW = 44 };
enum Sort { SORT_TITLE, SORT_RECENT };
const char *const sort_names[] = {"Title (A-Z)", "Recently played"};

struct Cart { std::string file, title; };

std::vector<Cart> carts;
std::vector<std::string> recent;
int selected;
float flow;
Sort sort_mode = SORT_TITLE;
bool loaded_config;
std::string toast;
unsigned toast_until, title_since;

std::string config_path() { return app::data_dir() + "/library.cfg"; }
std::string recent_path() { return app::data_dir() + "/recent.txt"; }

void load_config() {
    if (loaded_config) return;
    loaded_config = true;
    for (const auto &line : app::read_lines(config_path(), 16)) {
        if (line == "sort=recent") sort_mode = SORT_RECENT;
        if (line == "controls=hidden") q2_overlay_set_visible(0);
    }
    recent = app::read_lines(recent_path(), 50);
}
void save_config() {
    app::write_lines(config_path(), {sort_mode == SORT_RECENT ? "sort=recent" : "sort=title",
                                     q2_overlay_visible() ? "controls=shown" : "controls=hidden"});
}

std::string lower(std::string s) {
    for (char &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

void scan(const std::string &keep) {
    carts.clear();
    for (const auto &file : app::list_carts()) carts.push_back({file, ui::ascii(app::title_of(file))});
    auto by_title = [](const Cart &a, const Cart &b) {
        std::string la = lower(a.title), lb = lower(b.title);
        return la != lb ? la < lb : a.file < b.file;
    };
    std::sort(carts.begin(), carts.end(), by_title);
    if (sort_mode == SORT_RECENT) {
        auto rank = [](const Cart &c) {
            auto it = std::find(recent.begin(), recent.end(), c.file);
            return it == recent.end() ? recent.size() : (size_t)(it - recent.begin());
        };
        std::stable_sort(carts.begin(), carts.end(),
                         [&](const Cart &a, const Cart &b) { return rank(a) < rank(b); });
    }
    selected = 0;
    for (size_t i = 0; i < carts.size(); ++i)
        if (carts[i].file == keep) selected = (int)i;
    flow = (float)selected;
    covers::clear();
}

void notify(const std::string &message) {
    toast = message;
    toast_until = q2_millis() + 2600;
}

void select(int index) {
    if (carts.empty()) return;
    index = std::max(0, std::min((int)carts.size() - 1, index));
    if (index != selected) {
        title_since = q2_millis();
        Logger_Write("library: selected %d %s\n", index, carts[index].file.c_str());
    }
    selected = index;
}

const char *hint() {
    return q2_input_full() ? "Centre: play     Play/Pause: menu     Return: exit"
                           : "Centre: play     Play: menu     Back: exit";
}

void draw_shelf(unsigned now) {
    uint32_t *canvas = ui::canvas();
    ui::clear(ui::BLACK);
    covers::begin_frame();
    if (carts.empty()) {
        ui::caption(96, "No carts yet", ui::WHITE, ui::LARGE);
        ui::caption(140, "Copy .p8 or .p8.png carts to", ui::GRAY, ui::SMALL);
        ui::caption(160, "/.pico8/carts on the SD card,", ui::GRAY, ui::SMALL);
        ui::caption(180, "or open the menu and choose Get Games.", ui::GRAY, ui::SMALL);
    } else {
        int mid = (int)floorf(flow + 0.5f);
        int left = std::max(0, mid - 4), right = std::min((int)carts.size() - 1, mid + 4);
        /* Paint farther cards first so the active cover overlaps its neighbours. */
        while (left <= right) {
            int i = flow - left > right - flow ? left++ : right--;
            float d = (float)i - flow;
            coverflow::draw(canvas, covers::get(carts[i].file, d), d);
        }
        const std::string &title = carts[std::max(0, std::min((int)carts.size() - 1, mid))].title;
        int overflow = ui::text_width(title, ui::LARGE) - (ui::W - 36);
        ui::caption(TITLE_Y, title, ui::WHITE, ui::LARGE, ui::marquee(overflow, now - title_since));
        std::string sub = std::to_string(mid + 1) + " of " + std::to_string(carts.size());
        if (sort_mode == SORT_RECENT) sub += "  -  Recently played";
        ui::caption(SUB_Y, sub, ui::GRAY, ui::MEDIUM);
    }
    ui::fill(0, 0, ui::W, HEADER, ui::BLACK);
    ui::text(16, 12, "PICO-8", ui::WHITE, ui::MEDIUM);
    ui::ellipsis(ui::W - 30, 22, ui::GRAY);
    ui::caption(HINT_Y, hint(), ui::DIM, ui::SMALL);
    if (toast_until && (int)(now - toast_until) < 0) {
        int w = std::min(ui::W - 24, ui::text_width(toast, ui::SMALL) + 28);
        ui::fill((ui::W - w) / 2, HINT_Y - 8, w, 30, 0x242424);
        ui::caption(HINT_Y - 2, ui::fit(toast, ui::SMALL, w - 16), ui::WHITE, ui::SMALL);
    }
}

/* --- menu ------------------------------------------------------------ */
enum MenuItem { M_GET, M_SORT, M_CONTROLS, M_REFRESH, M_EXIT, M_COUNT };

std::string menu_label(int i) {
    switch (i) {
    case M_GET: return "Get Games from itch.io";
    case M_SORT: return std::string("Sort: ") + sort_names[sort_mode];
    case M_CONTROLS: return q2_overlay_visible() ? "Touch controls: Shown" : "Touch controls: Hidden";
    case M_REFRESH: return "Refresh carts and covers";
    default: return "Exit to Q2 Pod";
    }
}
int menu_top() { return (ui::H - M_COUNT * MENU_ROW) / 2 + 10; }

void draw_menu(int focus) {
    ui::shade(0, 0, ui::W, ui::H, ui::BLACK, 190);
    int x = (ui::W - MENU_W) / 2, top = menu_top();
    ui::text(x, top - 34, "Menu", ui::WHITE, ui::MEDIUM);
    for (int i = 0; i < M_COUNT; ++i)
        ui::button(x, top + i * MENU_ROW, MENU_W, MENU_ROW - 6, menu_label(i), true, i == focus);
}

int menu_at(int x, int y) {
    int left = (ui::W - MENU_W) / 2, top = menu_top();
    if (x < left || x >= left + MENU_W || y < top) return -1;
    int row = (y - top) / MENU_ROW;
    return row < M_COUNT && (y - top) % MENU_ROW < MENU_ROW - 6 ? row : -1;
}

/* Returns true to leave the app. */
bool activate(int item, LibraryChoice &choice) {
    std::string keep = carts.empty() ? "" : carts[selected].file;
    switch (item) {
    case M_GET: {
        std::string added = getgames_run();
        scan(added.empty() ? keep : added);
        if (!added.empty()) {
            Logger_Write("library: added %s\n", added.c_str());
            notify("Added " + ui::ascii(app::title_of(added)));
        }
        return false;
    }
    case M_SORT:
        sort_mode = sort_mode == SORT_TITLE ? SORT_RECENT : SORT_TITLE;
        save_config();
        scan(keep);
        return false;
    case M_CONTROLS:
        q2_overlay_set_visible(!q2_overlay_visible());
        save_config();
        return false;
    case M_REFRESH:
        scan(keep);
        notify(std::to_string(carts.size()) + (carts.size() == 1 ? " cart" : " carts"));
        return false;
    default:
        choice.exit = true;
        return true;
    }
}

/* Index of the cover under (x, y): the centre wins, then nearer covers. */
int cover_at(int x, int y) {
    int mid = (int)floorf(flow + 0.5f);
    for (int k = 0; k <= 8; ++k) {
        int i = mid + (k % 2 ? -(k + 1) / 2 : k / 2);
        if (i < 0 || i >= (int)carts.size()) continue;
        float d = (float)i - flow;
        if (coverflow::hit(covers::get(carts[i].file, d), d, x, y)) return i;
    }
    return -1;
}
} // namespace

void library_played(const std::string &cart) {
    load_config();
    recent.erase(std::remove(recent.begin(), recent.end(), cart), recent.end());
    recent.insert(recent.begin(), cart);
    if (recent.size() > 50) recent.resize(50);
    app::write_lines(recent_path(), recent);
}

LibraryChoice library_run(const std::string &keep, const std::string &message) {
    load_config();
    /* Remember a show/hide made with the in-game toggle. */
    if (q2_overlay_take_changed()) save_config();
    covers::set_dirs(app::cart_dir(), app::art_dir());
    scan(keep);
    Logger_Write("library: %zu carts, selected %d\n", carts.size(), selected);
    if (!message.empty()) notify(message);
    LibraryChoice choice;
    bool menu = false, dragging = false, dirty = true;
    int focus = 0, drag_x = 0;
    unsigned last_scan = q2_millis();
    float drag_flow = 0;
    title_since = q2_millis();
    q2_set_game_mode(0);
    while (true) {
        unsigned now = q2_millis();
        q2_input in = q2_read_input();
        if (in.quit) { choice.exit = true; break; }
        if (menu) {
            if (in.pressed & (Q2_PAUSE | Q2_X)) menu = false;
            if (in.pressed & Q2_LEFT) focus = (focus + M_COUNT - 1) % M_COUNT;
            if (in.pressed & Q2_RIGHT) focus = (focus + 1) % M_COUNT;
            int tapped = in.touch_ended ? menu_at(in.touch_x, in.touch_y) : -2;
            if (tapped == -1) menu = false;
            if (tapped >= 0) focus = tapped;
            if ((in.pressed & Q2_O) || tapped >= 0) {
                menu = false;
                if (activate(focus, choice)) break;
                q2_set_game_mode(0);
                dirty = true;
                continue;
            }
        } else {
            if (in.pressed & Q2_LEFT) select(selected - 1);
            if (in.pressed & Q2_RIGHT) select(selected + 1);
            if (in.pressed & Q2_X) { menu = true; focus = 0; }
            if (in.pressed & Q2_PAUSE) { choice.exit = true; break; }
            if ((in.pressed & Q2_O) && !carts.empty()) {
                choice.cart = carts[selected].file;
                break;
            }
            if (in.touch_began) { drag_x = in.touch_start_x; drag_flow = flow; dragging = false; }
            if (in.touch && !carts.empty()) {
                int dx = in.touch_x - drag_x;
                if (!dragging && abs(dx) > 10 && in.touch_start_y > HEADER) dragging = true;
                if (dragging) {
                    flow = drag_flow - (float)dx / coverflow::DRAG_PIXELS;
                    flow = std::max(-0.4f, std::min((float)carts.size() - 0.6f, flow));
                    select((int)floorf(flow + 0.5f));
                }
            }
            if (in.touch_ended) {
                if (dragging) dragging = false;
                else if (in.touch_y < HEADER && in.touch_x > ui::W - 70) { menu = true; focus = 0; }
                else {
                    int i = cover_at(in.touch_x, in.touch_y);
                    if (i >= 0 && i == selected && fabsf(flow - (float)selected) < 0.3f) {
                        choice.cart = carts[selected].file;
                        break;
                    }
                    if (i >= 0) select(i);
                }
            }
        }
        /* New carts copied over USB or by Get Games appear by themselves. */
        if (now - last_scan > 5000 && !menu && !dragging) {
            last_scan = now;
            std::vector<std::string> files = app::list_carts();
            if (files.size() != carts.size()) scan(carts.empty() ? "" : carts[selected].file);
        }
        bool moving = false;
        if (!dragging) {
            float d = (float)selected - flow;
            moving = fabsf(d) >= 0.01f;
            flow = moving ? flow + d * 0.38f : (float)selected;
        }
        bool input = in.pressed || in.touch || in.touch_began || in.touch_ended;
        bool scrolling_title = !carts.empty() &&
            ui::text_width(carts[selected].title, ui::LARGE) > ui::W - 36;
        bool toasting = toast_until && (int)(now - toast_until) < 200;
        /* Redraw only when something changes: an idle library sleeps. */
        if (dirty || input || moving || dragging || scrolling_title || toasting) {
            draw_shelf(now);
            if (menu) draw_menu(focus);
            q2_present_canvas();
            static bool first_shelf = true;
            if (first_shelf) {
                first_shelf = false;
                q2_debug_stage(7, 0xfff1e8, "first Cover Flow frame");
            }
            dirty = false;
        }
        if (covers::pump(covers::pending() ? 24 : 0)) dirty = true;
        if (!toasting && toast_until) toast_until = 0, dirty = true;
        unsigned spent = q2_millis() - now;
        unsigned frame = dirty || moving || dragging || scrolling_title || toasting ? FRAME_MS : 50;
        if (spent < frame) usleep((frame - spent) * 1000u);
    }
    if (choice.exit) Logger_Write("library: exit\n");
    else Logger_Write("library: launch %s\n", choice.cart.c_str());
    return choice;
}
