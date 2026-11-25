/* SPDX-License-Identifier: GPL-3.0-or-later
 * Get Games, after Pocket Boy's itch.io browser
 * (github.com/SlayTheEarth/q2-boy). The helper does all network work in
 * a child process; this screen stays responsive and Return cancels.
 *
 *   Previous / Next   move        Centre   choose
 *   Return                    back        touch    tap a row
 */
#include "getgames.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#include "app.h"
#include "itch.h"
#include "q2_input.h"
#include "ui.h"
#include "wheel_scroll.h"

namespace {
enum { FRAME_MS = 33, TOP = 48, ROW = 45, VISIBLE = 6 };
const char *const sort_keys[] = {"popular", "newest", "top-rated"};
const char *const sort_names[] = {"Popular", "Newest", "Top rated"};
const char *const price_keys[] = {"free", "all"};
const char *const price_names[] = {"Free", "Free and paid"};

enum Mode { LIST, DETAIL, QR };
enum Job { NONE, CATALOG, DOWNLOAD, QRCODE };

struct State {
    int sort = 0, price = 0, page = 1;
    std::vector<ItchGame> games;
    bool more = false, loaded = false;
    std::string status;      /* shown instead of the list (loading, errors) */
    bool status_error = false;
    int focus = 0, scroll = 0;
    Mode mode = LIST;
    int detail_focus = 0;
    std::string message;     /* result line in the detail panel */
    unsigned char qr[ITCH_QR_LIMIT][ITCH_QR_LIMIT];
    int qr_size = 0;
    Job job = NONE;
    std::string added;
} st;

ItchJob job_state;
ItchGame games_buf[ITCH_LIMIT];
std::vector<std::string> files_cache;

bool helper_present() {
    struct stat s;
    return stat(app::helper().c_str(), &s) == 0 && (s.st_mode & 0111);
}

bool installed(const ItchGame &g) {
    std::string tag = " [itch " + std::to_string(g.id) + "].";
    for (const auto &f : files_cache)
        if (f.find(tag) != std::string::npos) return true;
    return false;
}
bool free_game(const ItchGame &g) {
    return !strcmp(g.price, "Free") || !strncmp(g.price, "Free /", 6);
}

/* Rows: sort, price, then the games and paging, or "Try again" after an
 * error or a cancelled page. */
bool retry_row() { return st.job != CATALOG && st.games.empty() && st.status_error; }
int row_count() {
    if (st.job == CATALOG) return 2;
    if (retry_row()) return 3;
    return 2 + (int)st.games.size() + (st.more ? 1 : 0) + (st.page > 1 ? 1 : 0);
}
enum RowKind { R_SORT, R_PRICE, R_GAME, R_NEXT, R_PREV, R_RETRY };
RowKind row_kind(int row, int *game) {
    if (row == 0) return R_SORT;
    if (row == 1) return R_PRICE;
    if (retry_row()) return R_RETRY;
    int g = row - 2;
    if (g < (int)st.games.size()) { *game = g; return R_GAME; }
    g -= (int)st.games.size();
    if (st.more && g == 0) return R_NEXT;
    return R_PREV;
}

bool start(Job kind, std::vector<std::string> args) {
    std::string helper = app::helper();
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(helper.c_str()));
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    if (!itch_start(&job_state, helper.c_str(), argv.data())) return false;
    st.job = kind;
    return true;
}

void load_page() {
    st.games.clear();
    st.more = false;
    st.focus = std::min(st.focus, 1);
    st.scroll = 0;
    st.status_error = false;
    if (!helper_present()) {
        st.status = "Get Games needs q2-pico8-itch beside the app";
        st.status_error = true;
        return;
    }
    if (start(CATALOG, {"list", price_keys[st.price], sort_keys[st.sort],
                        std::to_string(st.page), ""}))
        st.status = "Checking itch.io page " + std::to_string(st.page) + "...";
    else {
        st.status = "Could not start the itch.io helper";
        st.status_error = true;
    }
}

std::string first_line(const char *text) {
    std::string s(text);
    size_t nl = s.find('\n');
    if (nl != std::string::npos) s.resize(nl);
    return ui::ascii(s);
}

void finish_job(bool ok) {
    Job kind = st.job;
    st.job = NONE;
    switch (kind) {
    case CATALOG: {
        if (!ok) {
            st.status = first_line(job_state.result);
            if (st.status.empty()) st.status = "Could not reach itch.io. Check Q2 Wi-Fi";
            st.status_error = true;
            return;
        }
        bool more = false;
        int n = itch_catalog(job_state.result, games_buf, &more);
        st.games.assign(games_buf, games_buf + n);
        st.more = more;
        st.loaded = true;
        st.status = n ? "" : "No downloadable PICO-8 carts on this page";
        st.status_error = false;
        if (n && st.focus < 2) st.focus = 2;
        return;
    }
    case DOWNLOAD: {
        st.message = first_line(job_state.result);
        if (ok) {
            files_cache = app::list_carts();
            if (st.message.empty()) st.message = "Cart added to your library";
        } else if (st.message.empty()) st.message = "The download failed";
        return;
    }
    case QRCODE:
        st.qr_size = ok ? itch_qr(job_state.result, st.qr) : 0;
        if (!st.qr_size) { st.mode = DETAIL; st.message = "Could not make the QR code"; }
        return;
    default:
        return;
    }
}

void draw_spinner(int y, unsigned now) {
    for (int i = 0; i < 3; ++i) {
        bool on = (now / 250) % 3 == (unsigned)i;
        ui::fill(ui::W / 2 - 22 + i * 18, y, 8, 8, on ? ui::WHITE : 0x444444);
    }
}

void draw_list(unsigned now) {
    ui::clear(ui::BLACK);
    ui::text(16, 10, "Get Games", ui::WHITE, ui::MEDIUM);
    std::string where = "itch.io  -  page " + std::to_string(st.page);
    ui::text(ui::W - 16 - ui::text_width(where, ui::SMALL), 14, where, ui::GRAY, ui::SMALL);
    int rows = row_count();
    if (st.focus < st.scroll) st.scroll = st.focus;
    if (st.focus >= st.scroll + VISIBLE) st.scroll = st.focus - VISIBLE + 1;
    int last = std::min(rows, st.scroll + VISIBLE);
    for (int row = st.scroll; row < last; ++row) {
        int y = TOP + (row - st.scroll) * ROW;
        bool on = row == st.focus;
        ui::fill(12, y, ui::W - 24, ROW - 5, on ? ui::ROW_ON : ui::ROW);
        int g = -1;
        switch (row_kind(row, &g)) {
        case R_SORT:
            ui::text(24, y + 11, std::string("Sort: ") + sort_names[st.sort], ui::WHITE, ui::SMALL);
            break;
        case R_PRICE:
            ui::text(24, y + 11, std::string("Price: ") + price_names[st.price], ui::WHITE, ui::SMALL);
            break;
        case R_NEXT:
            ui::text(24, y + 11, "Next page", ui::WHITE, ui::SMALL);
            break;
        case R_PREV:
            ui::text(24, y + 11, "Previous page", ui::WHITE, ui::SMALL);
            break;
        case R_RETRY:
            ui::text(24, y + 11, "Try again", ui::WHITE, ui::SMALL);
            break;
        case R_GAME: {
            const ItchGame &game = st.games[g];
            bool have = installed(game);
            int right = ui::W - 24 - (have ? 70 : 0);
            ui::text(24, y + 3, ui::fit(ui::ascii(game.title), ui::SMALL, right - 24), ui::WHITE,
                     ui::SMALL, 0, right);
            std::string line = ui::ascii(game.creator) + "  -  " + ui::ascii(game.price);
            if (game.web) line += "  -  Web";
            ui::text(24, y + 21, ui::fit(line, ui::SMALL, right - 24), ui::GRAY, ui::SMALL, 0, right);
            if (have) ui::text(ui::W - 86, y + 11, "Installed", ui::GREEN, ui::SMALL);
            break;
        }
        }
    }
    if (st.status.empty()) return;
    int y = TOP + 3 * ROW + 16;
    if (st.job == CATALOG) {
        y = TOP + 2 * ROW + 30;
        ui::caption(y, st.status, ui::WHITE, ui::SMALL);
        ui::caption(y + 22, "Verifying downloadable carts can take a while", ui::DIM, ui::SMALL);
        draw_spinner(y + 56, now);
        ui::caption(y + 82, "Return: cancel", ui::DIM, ui::SMALL);
    } else if (st.games.empty()) {
        if (!st.status_error) y = TOP + 2 * ROW + 30;
        ui::caption(y, ui::fit(st.status, ui::SMALL, ui::W - 40),
                    st.status_error ? ui::PINK : ui::WHITE, ui::SMALL);
        if (!st.status_error)
            ui::caption(y + 24, "Try another page, sort or price", ui::DIM, ui::SMALL);
    }
}

enum DetailButton { D_DOWNLOAD, D_VIEW, D_BACK, D_COUNT };
int detail_y(int i) { return 170 + i * 44; }

void draw_detail(unsigned now) {
    const ItchGame &g = st.games[std::max(0, st.focus - 2)];
    ui::shade(0, 0, ui::W, ui::H, ui::BLACK, 220);
    ui::fill(12, 24, ui::W - 24, ui::H - 40, 0x121212);
    ui::text(28, 36, ui::fit(ui::ascii(g.title), ui::MEDIUM, ui::W - 56), ui::WHITE, ui::MEDIUM);
    ui::text(28, 66, ui::fit("by " + ui::ascii(g.creator), ui::SMALL, ui::W - 56), ui::GRAY, ui::SMALL);
    ui::text(28, 86, ui::ascii(g.price), free_game(g) ? ui::GREEN : ui::WHITE, ui::SMALL);
    const char *note = g.web ? "Browser game: saved as a .p8.png cart" : "Downloadable PICO-8 cart";
    if (!free_game(g)) note = "Paid: buy it on itch.io, then copy the cart over";
    ui::text(28, 106, note, ui::DIM, ui::SMALL);
    if (st.job == DOWNLOAD) {
        ui::text(28, 132, "Downloading...", ui::WHITE, ui::SMALL);
        draw_spinner(138, now);
    } else if (!st.message.empty()) {
        ui::text(28, 132, ui::fit(st.message, ui::SMALL, ui::W - 56), ui::WHITE, ui::SMALL);
    }
    bool have = installed(g);
    const char *labels[] = {have ? "Installed" : "Download", "View on itch.io", "Back"};
    bool enabled[] = {free_game(g) && !have && st.job == NONE, true, true};
    for (int i = 0; i < D_COUNT; ++i)
        ui::button(40, detail_y(i), ui::W - 80, 38, labels[i], enabled[i], st.detail_focus == i);
}

void draw_qr() {
    const ItchGame &g = st.games[std::max(0, st.focus - 2)];
    ui::clear(ui::BLACK);
    int n = st.qr_size;
    int module = std::max(1, std::min(6, 236 / (n + 8)));
    int size = (n + 8) * module, x0 = (ui::W - size) / 2, y0 = 12;
    ui::fill(x0, y0, size, size, ui::WHITE);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (st.qr[y][x]) ui::fill(x0 + (x + 4) * module, y0 + (y + 4) * module, module, module, ui::BLACK);
    ui::caption(y0 + size + 10, "Scan to open on your phone", ui::WHITE, ui::SMALL);
    ui::caption(y0 + size + 30, ui::fit(ui::ascii(g.url), ui::SMALL, ui::W - 30), ui::GRAY, ui::SMALL);
}

void cancel_job() {
    if (st.job == NONE) return;
    itch_stop(&job_state);
    st.job = NONE;
}

void open_row(int row) {
    int g = -1;
    switch (row_kind(row, &g)) {
    case R_SORT: st.sort = (st.sort + 1) % 3; st.page = 1; load_page(); break;
    case R_PRICE: st.price = (st.price + 1) % 2; st.page = 1; load_page(); break;
    case R_NEXT: ++st.page; st.focus = 2; load_page(); break;
    case R_PREV: if (st.page > 1) --st.page; st.focus = 2; load_page(); break;
    case R_RETRY: load_page(); break;
    case R_GAME:
        st.mode = DETAIL;
        st.message.clear();
        st.detail_focus = free_game(st.games[g]) && !installed(st.games[g]) ? D_DOWNLOAD : D_VIEW;
        break;
    }
}

void choose_detail(int button) {
    const ItchGame &g = st.games[std::max(0, st.focus - 2)];
    if (button == D_BACK) { st.mode = LIST; return; }
    if (button == D_VIEW) {
        if (st.job != NONE) return;
        st.qr_size = 0;
        if (start(QRCODE, {"qr", g.url})) st.mode = QR;
        else st.message = "Could not start the itch.io helper";
        return;
    }
    if (!free_game(g) || installed(g) || st.job != NONE) return;
    mkdir(app::cart_dir().c_str(), 0755);
    mkdir(app::art_dir().c_str(), 0755);
    st.message.clear();
    if (!start(DOWNLOAD, {"download", g.url, app::cart_dir(), app::art_dir()}))
        st.message = "Could not start the itch.io helper";
}
} // namespace

std::string getgames_run() {
    signal(SIGPIPE, SIG_IGN);
    files_cache = app::list_carts();
    st.added.clear();
    st.mode = LIST;
    if (!st.loaded && st.job == NONE) load_page();
    WheelScroll wheel;
    std::vector<std::string> before = files_cache;
    while (true) {
        unsigned now = q2_millis();
        bool ok = false;
        if (st.job != NONE && itch_finished(&job_state, &ok)) finish_job(ok);
        q2_input in = q2_read_input();
        if (in.quit) { cancel_job(); q2_input_quit(); break; }
        Mode previous_mode = st.mode;
        int steps = in.touch ? 0 : wheel.steps(in.wheel, now);
        if (in.pressed & Q2_LEFT) --steps;
        if (in.pressed & Q2_RIGHT) ++steps;
        if (st.mode == QR) {
            if (in.pressed & (Q2_PAUSE | Q2_O | Q2_X) || in.touch_ended) {
                if (st.job == QRCODE) cancel_job();
                st.mode = DETAIL;
            }
        } else if (st.mode == DETAIL) {
            if (in.pressed & Q2_PAUSE) {
                if (st.job == DOWNLOAD) { cancel_job(); st.message = "Download cancelled"; }
                else st.mode = LIST;
            }
            st.detail_focus = std::max(0, std::min(D_COUNT - 1, st.detail_focus + steps));
            if (in.pressed & Q2_O) choose_detail(st.detail_focus);
            if (in.touch_ended)
                for (int i = 0; i < D_COUNT; ++i)
                    if (in.touch_y >= detail_y(i) && in.touch_y < detail_y(i) + 38 &&
                        in.touch_x >= 40 && in.touch_x < ui::W - 40) {
                        st.detail_focus = i;
                        choose_detail(i);
                    }
        } else {
            if (in.pressed & Q2_PAUSE) {
                if (st.job == CATALOG) {
                    cancel_job();
                    st.status = "Cancelled";
                    st.status_error = true;
                } else break;
            }
            int rows = row_count();
            st.focus = std::max(0, std::min(rows - 1, st.focus + steps));
            int tapped = -1;
            if (in.touch_ended && in.touch_y >= TOP && (in.touch_y - TOP) % ROW < ROW - 5) {
                int row = st.scroll + (in.touch_y - TOP) / ROW;
                if (row < rows) tapped = st.focus = row;
            }
            if ((in.pressed & Q2_O) || tapped >= 0) {
                /* Sort and price restart a page that is still loading. */
                if (st.job == CATALOG) cancel_job();
                if (st.job == NONE) open_row(st.focus);
            }
        }
        if (st.mode != previous_mode) wheel = {};
        if (st.mode == QR && st.qr_size) draw_qr();
        else {
            draw_list(now);
            if (st.mode != LIST) draw_detail(now);
        }
        q2_present_canvas();
        unsigned spent = q2_millis() - now;
        unsigned frame = st.job != NONE || in.pressed || in.touch ? FRAME_MS : 60;
        if (spent < frame) usleep((frame - spent) * 1000u);
    }
    std::vector<std::string> after = app::list_carts();
    for (const auto &f : after)
        if (std::find(before.begin(), before.end(), f) == before.end()) st.added = f;
    return st.added;
}
