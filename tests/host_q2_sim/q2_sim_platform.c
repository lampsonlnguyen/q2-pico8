/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host Q2 simulation platform for the PICO-8 player.
 *
 * This is NOT the device code and it is NOT an emulator: it runs the real
 * FAKE-08 VM and the real src/fake08_host.cpp on x86_64, replacing only
 * the Q2 hardware layer that src/q2_platform.c abstracts — the /dev/fb0
 * framebuffer, the ALSA device, and the GPIO/input socket.
 *
 * Why q2_platform.c itself cannot be compiled here: it is MIPS-only
 * (raw MIPS o32 syscall numbers, inline MIPS asm for `sc`, and a MIPS
 * __start entry). This file re-implements the same q2_platform.h
 * interface with the same semantics, ported from q2_platform.c:
 *  - present_mapped(): identical 2x scaling, centring, rotated
 *    bottom-up storage, palette mapping (map[color] & 0x8f for the
 *    FAKE-08 128..143 alternate colours), and the same on-screen
 *    control overlay (same positions, glyphs, active highlight).
 *  - q2_read_input(): identical datagram semantics — 'w <n>' wheel
 *    synthesises brief LEFT/RIGHT presses, key codes 218=O, 171=X,
 *    222/172=LEFT, 223/173=RIGHT, code 170 (Back) gives PAUSE on a
 *    single press and quit on a second press within 500 ms, touch
 *    't d x y' hits the same control rectangles. The datagrams here
 *    come from a script file (Q2_SIM_INPUT) instead of a Unix socket,
 *    scheduled by real wall-clock time since q2_open().
 *  - q2_millis(): real CLOCK_MONOTONIC time, NOT the +34 ms fake time
 *    in tests/q2_fake08_stub.c — so frame pacing is genuinely measured.
 *  - ALSA: replaced by a WAV capture sink (Q2_SIM_OUTDIR/audio.wav,
 *    22050 Hz stereo s16). It never blocks, like the device's
 *    non-blocking ALSA write, and records peak/RMS/frames written.
 *  - Framebuffer: a 375x320 in-memory buffer dumped to PNG on every
 *    present, with a per-frame timestamp and hash for pacing analysis.
 *
 * It also supplies sc(), the syscall shim that src/cart_browser.c needs:
 * the real cart_browser.c is compiled unchanged on the host, and sc()
 * implements only the three calls it makes — SYS_OPEN (4005),
 * SYS_CLOSE (4006) and SYS_GETDENTS64 (4219) — on top of POSIX
 * opendir/readdir. No other device code is involved.
 *
 * Environment (all optional):
 *   Q2_SIM_OUTDIR     output dir for frames/*.png, audio.wav, stats.json
 *                       (default: build/host-sim-out)
 *   Q2_SIM_INPUT      input script path (see scripts/*.txt for the format)
 *   Q2_SIM_MAX_FRAMES stop after this many presented frames (0 = no limit;
 *                       the run then ends only via a scripted double-Back)
 *   Q2_SIM_DUMP_EVERY write a PNG every Nth frame (default 1)
 *
 * This file is host-only (Q2_HOST_SIM build) and is never linked into
 * the device / freestanding builds, so it cannot affect them.
 */
#define _POSIX_C_SOURCE 200809L
#include "q2_platform.h"

#include <dirent.h>
#include <math.h>
#include <png.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* --- on-screen controls: identical table to src/q2_platform.c -------- */
enum { CONTROL_SIZE = 32, CONTROL_X_LEFT = 12, CONTROL_X_RIGHT = 331 };
static const struct {
    int x, y;
    unsigned key;
    uint8_t glyph[7];
} controls[] = {
    {CONTROL_X_LEFT, 70, Q2_UP,    {8,28,62,8,8,8,8}},
    {CONTROL_X_LEFT, 120,Q2_LEFT,  {0,8,16,62,16,8,0}},
    {CONTROL_X_LEFT, 170,Q2_RIGHT, {0,8,4,62,4,8,0}},
    {CONTROL_X_LEFT, 220,Q2_DOWN,  {8,8,8,8,62,28,8}},
    {CONTROL_X_RIGHT,120,Q2_O,     {0,28,34,34,34,28,0}},
    {CONTROL_X_RIGHT,170,Q2_X,     {0,34,20,8,20,34,0}},
};

/* --- state ------------------------------------------------------------ */
static q2_pixel *fb_mem;
static q2_input input_state;
static unsigned physical_buttons, touch_button, back_at, back_pending;
static int quit_flag;
static char exit_reason[32] = "not-exited";

static struct timespec start_ts;
static int have_start;

static char outdir[512] = "build/host-sim-out";
static FILE *wav_file;
static int audio_opened;
static unsigned audio_frames_written, audio_peak;
static double audio_sumsq;
static unsigned audio_hist_367, audio_hist_368, audio_hist_735, audio_hist_other;

static unsigned frame_count, distinct_frames;
static uint64_t last_hash;
static unsigned dump_every = 1;

/* frame log: timestamp (ms) + packed-pixel hash per present */
static unsigned *frame_ms;
static uint64_t *frame_hash;
static size_t frame_log_cap, frame_log_len;

static unsigned input_events_applied;

/* input script */
enum { EV_WHEEL, EV_KEY_DOWN, EV_KEY_UP, EV_BACK, EV_TOUCH_DOWN, EV_TOUCH_UP, EV_QUIT };
typedef struct { unsigned ms; int kind; int a, b; } sim_event;
static sim_event *events;
static size_t event_count, event_next;
static unsigned max_frames;

static void mkdir_p(const char *path) {
    char tmp[1024];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static unsigned monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!have_start) { start_ts = ts; have_start = 1; return 0; }
    long s = (long)(ts.tv_sec - start_ts.tv_sec);
    long ns = (long)(ts.tv_nsec - start_ts.tv_nsec);
    if (ns < 0) { s -= 1; ns += 1000000000L; }
    return (unsigned)(s * 1000L + ns / 1000000L);
}

unsigned q2_millis(void) { return monotonic_ms(); }

/* --- input script ----------------------------------------------------- */
static unsigned key_bit(const char *name) {
    if (!strcmp(name, "LEFT")) return Q2_LEFT;
    if (!strcmp(name, "RIGHT")) return Q2_RIGHT;
    if (!strcmp(name, "UP")) return Q2_UP;
    if (!strcmp(name, "DOWN")) return Q2_DOWN;
    if (!strcmp(name, "O")) return Q2_O;
    if (!strcmp(name, "X")) return Q2_X;
    return 0;
}

static void load_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        sim_event ev = {0, -1, 0, 0};
        char word[32] = {0}, name[32] = {0}, dir[16] = {0};
        unsigned ms = 0;
        if (sscanf(p, "%u %31s", &ms, word) != 2) continue;
        ev.ms = ms;
        if (!strcmp(word, "wheel")) {
            int d = 0;
            if (sscanf(p, "%*u %*s %d", &d) == 1) { ev.kind = EV_WHEEL; ev.a = d; }
        } else if (!strcmp(word, "key")) {
            if (sscanf(p, "%*u %*s %31s %15s", name, dir) == 2) {
                if (!strcmp(name, "BACK")) ev.kind = EV_BACK;
                else if (!strcmp(dir, "down")) { ev.kind = EV_KEY_DOWN; ev.a = (int)key_bit(name); }
                else if (!strcmp(dir, "up")) { ev.kind = EV_KEY_UP; ev.a = (int)key_bit(name); }
                else if (!strcmp(dir, "press")) { ev.kind = EV_KEY_DOWN; ev.a = (int)key_bit(name); }
            }
        } else if (!strcmp(word, "touch")) {
            int x = 0, y = 0;
            if (sscanf(p, "%*u %*s %d %d %15s", &x, &y, dir) == 3) {
                ev.kind = !strcmp(dir, "down") ? EV_TOUCH_DOWN : EV_TOUCH_UP;
                ev.a = x; ev.b = y;
            }
        } else if (!strcmp(word, "quit")) {
            ev.kind = EV_QUIT;
        }
        if (ev.kind < 0) continue;
        events = realloc(events, (event_count + 1) * sizeof *events);
        events[event_count++] = ev;
    }
    fclose(f);
    /* insertion sort by ms (scripts are small and usually already sorted) */
    for (size_t i = 1; i < event_count; ++i) {
        sim_event e = events[i];
        size_t j = i;
        while (j > 0 && events[j - 1].ms > e.ms) { events[j] = events[j - 1]; --j; }
        events[j] = e;
    }
}

/* --- WAV sink (replaces ALSA) ------------------------------------------ */
static void wav_write_header_placeholder(void) {
    static const unsigned char hdr[44] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
        0x22,0x56,0,0, 0x44,0xac,0,0, 4,0, 16,0,
        'd','a','t','a', 0,0,0,0 };
    fwrite(hdr, 1, sizeof hdr, wav_file);
}

static void wav_finalise(void) {
    if (!wav_file) return;
    unsigned data_bytes = audio_frames_written * 2u * 2u;
    unsigned char sz[4];
    fseek(wav_file, 4, SEEK_SET);
    unsigned riff = 36u + data_bytes;
    sz[0] = riff & 0xff; sz[1] = (riff >> 8) & 0xff; sz[2] = (riff >> 16) & 0xff; sz[3] = (riff >> 24) & 0xff;
    fwrite(sz, 1, 4, wav_file);
    fseek(wav_file, 40, SEEK_SET);
    sz[0] = data_bytes & 0xff; sz[1] = (data_bytes >> 8) & 0xff; sz[2] = (data_bytes >> 16) & 0xff; sz[3] = (data_bytes >> 24) & 0xff;
    fwrite(sz, 1, 4, wav_file);
    fclose(wav_file);
    wav_file = NULL;
}

int q2_audio_open(void) {
    if (audio_opened) return 0;
    char path[1024];
    snprintf(path, sizeof path, "%s/audio.wav", outdir);
    wav_file = fopen(path, "wb");
    if (!wav_file) return -1;
    wav_write_header_placeholder();
    audio_opened = 1;
    return 0;
}

void q2_audio_close(void) {
    if (!audio_opened) return;
    wav_finalise();
    audio_opened = 0;
}

int q2_audio_available(void) { return audio_opened; }

void q2_audio_write(const int16_t *samples, unsigned frames) {
    if (!audio_opened || !samples || !frames) return;
    /* WAV sink always accepts, like a non-blocking ALSA write that
     * never returns -EAGAIN in this harness; the device drops a chunk
     * on a full buffer instead — that path is device-only. */
    fwrite(samples, sizeof(int16_t), (size_t)frames * 2u, wav_file);
    for (unsigned i = 0; i < frames * 2u; ++i) {
        int v = samples[i];
        unsigned a = v < 0 ? (unsigned)(-v) : (unsigned)v;
        if (a > audio_peak) audio_peak = a;
        audio_sumsq += (double)v * (double)v;
    }
    audio_frames_written += frames;
    if (frames == 367) ++audio_hist_367;
    else if (frames == 368) ++audio_hist_368;
    else if (frames == 735) ++audio_hist_735;
    else ++audio_hist_other;
}

/* --- framebuffer present: port of src/q2_platform.c present_mapped ---- */
static uint64_t fnv1a(const uint8_t *data, size_t len, uint64_t h) {
    for (size_t i = 0; i < len; ++i) { h ^= data[i]; h *= 1099511628211ULL; }
    return h;
}

static int write_png(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (!png || !info) { fclose(f); return -1; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); fclose(f); return -1; }
    png_init_io(png, f);
    png_set_IHDR(png, info, Q2_WIDTH, Q2_HEIGHT, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep row = malloc(Q2_WIDTH * 3);
    for (unsigned y = 0; y < Q2_HEIGHT; ++y) {
        for (unsigned x = 0; x < Q2_WIDTH; ++x) {
            q2_pixel px = fb_mem[x * Q2_HEIGHT + (Q2_HEIGHT - 1 - y)];
            row[x * 3 + 0] = (uint8_t)(px >> 16);
            row[x * 3 + 1] = (uint8_t)(px >> 8);
            row[x * 3 + 2] = (uint8_t)px;
        }
        png_write_row(png, row);
    }
    free(row);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return 0;
}

static void present_mapped(const uint8_t *pixels, const uint8_t *map, const uint32_t *palette) {
    if (!fb_mem || !pixels || !palette) return;
    for (unsigned x = 0; x < Q2_WIDTH; ++x) {
        q2_pixel *col = fb_mem + x * Q2_HEIGHT;
        int sx = ((int)x - (Q2_WIDTH - 256) / 2) / 2;
        for (unsigned y = 0; y < Q2_HEIGHT; ++y) {
            int sy = ((int)y - (Q2_HEIGHT - 256) / 2) / 2;
            uint32_t argb = 0xff000000u;
            if (x >= (Q2_WIDTH - 256) / 2 && x < (Q2_WIDTH - 256) / 2 + 256 &&
                y >= (Q2_HEIGHT - 256) / 2 && y < (Q2_HEIGHT - 256) / 2 + 256) {
                unsigned off = (unsigned)sy * 64u + (unsigned)sx / 2u;
                uint8_t data = pixels[off];
                unsigned color = (sx & 1) ? (data >> 4) : (data & 15u);
                unsigned mapped = map ? (map[color] & 0x8fu) : color;
                argb = 0xff000000u | palette[mapped];
            }
            col[Q2_HEIGHT - 1 - y] = argb;
        }
    }
    if (map) {
        for (unsigned i = 0; i < sizeof controls / sizeof controls[0]; ++i) {
            unsigned active = input_state.buttons & controls[i].key;
            for (int x = 0; x < CONTROL_SIZE; ++x) {
                q2_pixel *col = fb_mem + (controls[i].x + x) * Q2_HEIGHT;
                for (int y = 0; y < CONTROL_SIZE; ++y) {
                    unsigned ink = x >= 9 && x < 23 && y >= 9 && y < 23 &&
                        (controls[i].glyph[(y - 9) / 2] & (1u << (6 - (x - 9) / 2)));
                    col[Q2_HEIGHT - 1 - controls[i].y - y] = ink ? 0xffeeeeeeu :
                        active ? 0xff326977u : 0xff263247u;
                }
            }
        }
    }
    /* log + dump */
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a(pixels, 8192, h);
    if (map) h = fnv1a(map, 16, h);
    unsigned now = q2_millis();
    if (frame_log_len == frame_log_cap) {
        frame_log_cap = frame_log_cap ? frame_log_cap * 2 : 1024;
        frame_ms = realloc(frame_ms, frame_log_cap * sizeof *frame_ms);
        frame_hash = realloc(frame_hash, frame_log_cap * sizeof *frame_hash);
    }
    frame_ms[frame_log_len] = now;
    frame_hash[frame_log_len] = h;
    ++frame_log_len;
    if (h != last_hash) { ++distinct_frames; last_hash = h; }
    ++frame_count;
    if (dump_every && (frame_count - 1) % dump_every == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/frames/frame_%04u.png", outdir, frame_count);
        write_png(path);
    }
}

void q2_present(const uint8_t *pixels, const uint32_t palette[16]) {
    present_mapped(pixels, 0, palette);
}

void q2_present_fake08(const uint8_t *pixels, const uint8_t screen_map[16], const uint32_t colors[144]) {
    if (!screen_map) return;
    present_mapped(pixels, screen_map, colors);
}

/* --- open / close / input ---------------------------------------------- */
int q2_open(void) {
    input_state = (q2_input){0};
    physical_buttons = touch_button = back_pending = 0;
    back_at = 0;
    quit_flag = 0;
    strcpy(exit_reason, "not-exited");
    frame_count = distinct_frames = 0;
    last_hash = 0;
    frame_log_len = 0;
    audio_frames_written = audio_peak = 0;
    audio_sumsq = 0;
    audio_hist_367 = audio_hist_368 = audio_hist_735 = audio_hist_other = 0;
    input_events_applied = 0;
    event_next = 0;
    have_start = 0;
    (void)monotonic_ms();

    const char *od = getenv("Q2_SIM_OUTDIR");
    if (od && od[0]) snprintf(outdir, sizeof outdir, "%s", od);
    const char *de = getenv("Q2_SIM_DUMP_EVERY");
    if (de) { unsigned v = (unsigned)atoi(de); if (v) dump_every = v; }
    const char *mf = getenv("Q2_SIM_MAX_FRAMES");
    max_frames = mf ? (unsigned)atoi(mf) : 0;

    mkdir_p(outdir);
    char frames_dir[1024];
    snprintf(frames_dir, sizeof frames_dir, "%s/frames", outdir);
    mkdir_p(frames_dir);

    const char *script = getenv("Q2_SIM_INPUT");
    if (script && script[0]) load_script(script);

    fb_mem = calloc((size_t)Q2_WIDTH * Q2_HEIGHT, sizeof(q2_pixel));
    if (!fb_mem) return -1;
    return 0;
}

static void write_stats(void) {
    char path[1024];
    snprintf(path, sizeof path, "%s/stats.json", outdir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    double rms = audio_frames_written ? sqrt(audio_sumsq / ((double)audio_frames_written * 2.0)) : 0.0;
    fprintf(f, "{\n");
    fprintf(f, "  \"frames_presented\": %u,\n", frame_count);
    fprintf(f, "  \"distinct_frames\": %u,\n", distinct_frames);
    fprintf(f, "  \"exit_reason\": \"%s\",\n", exit_reason);
    fprintf(f, "  \"input_events_applied\": %u,\n", input_events_applied);
    fprintf(f, "  \"audio_frames_written\": %u,\n", audio_frames_written);
    fprintf(f, "  \"audio_seconds\": %.3f,\n", audio_frames_written / 22050.0);
    fprintf(f, "  \"audio_peak\": %u,\n", audio_peak);
    fprintf(f, "  \"audio_rms\": %.1f,\n", rms);
    fprintf(f, "  \"audio_calls_367\": %u,\n", audio_hist_367);
    fprintf(f, "  \"audio_calls_368\": %u,\n", audio_hist_368);
    fprintf(f, "  \"audio_calls_735\": %u,\n", audio_hist_735);
    fprintf(f, "  \"audio_calls_other\": %u,\n", audio_hist_other);
    fprintf(f, "  \"frame_ms\": [");
    for (size_t i = 0; i < frame_log_len; ++i) fprintf(f, "%s%u", i ? "," : "", frame_ms[i]);
    fprintf(f, "],\n");
    fprintf(f, "  \"frame_hash\": [");
    for (size_t i = 0; i < frame_log_len; ++i) fprintf(f, "%s%llu", i ? "," : "", (unsigned long long)frame_hash[i]);
    fprintf(f, "]\n}\n");
    fclose(f);
}

void q2_close(void) {
    q2_audio_close();
    write_stats();
    free(fb_mem); fb_mem = NULL;
    free(events); events = NULL; event_count = event_next = 0;
    free(frame_ms); frame_ms = NULL;
    free(frame_hash); frame_hash = NULL; frame_log_cap = frame_log_len = 0;
}

q2_input q2_read_input(void) {
    unsigned now = q2_millis();
    input_state.pressed = 0;
    input_state.wheel = 0;
    while (event_next < event_count && events[event_next].ms <= now) {
        sim_event ev = events[event_next++];
        ++input_events_applied;
        switch (ev.kind) {
        case EV_WHEEL:
            input_state.wheel += ev.a;
            if (ev.a > 0) input_state.pressed |= Q2_RIGHT;
            else if (ev.a < 0) input_state.pressed |= Q2_LEFT;
            break;
        case EV_KEY_DOWN:
            input_state.pressed |= (unsigned)ev.a & ~physical_buttons;
            physical_buttons |= (unsigned)ev.a;
            break;
        case EV_KEY_UP:
            physical_buttons &= ~(unsigned)ev.a;
            break;
        case EV_BACK:
            /* device: Back arrives as key-up code 170; single press =
             * PAUSE, second press within 500 ms = quit. */
            if (back_pending && now - back_at <= 500u) {
                quit_flag = 1;
                strcpy(exit_reason, "double-back");
            } else {
                input_state.pressed |= Q2_PAUSE;
                back_pending = 1;
                back_at = now;
            }
            break;
        case EV_TOUCH_DOWN: {
            touch_button = 0;
            for (unsigned i = 0; i < sizeof controls / sizeof controls[0]; ++i) {
                if (ev.a >= controls[i].x && ev.a < controls[i].x + CONTROL_SIZE &&
                    ev.b >= controls[i].y && ev.b < controls[i].y + CONTROL_SIZE) {
                    touch_button = controls[i].key;
                    input_state.pressed |= touch_button;
                    break;
                }
            }
            break;
        }
        case EV_TOUCH_UP:
            touch_button = 0;
            break;
        case EV_QUIT:
            quit_flag = 1;
            strcpy(exit_reason, "script-quit");
            break;
        }
    }
    if (max_frames && frame_count >= max_frames && !quit_flag) {
        quit_flag = 1;
        strcpy(exit_reason, "max-frames");
    }
    input_state.buttons = physical_buttons | touch_button;
    input_state.quit = (unsigned)quit_flag;
    return input_state;
}

/* --- sc() shim for the real src/cart_browser.c --------------------------
 * Only the calls cart_browser.c makes: SYS_OPEN 4005, SYS_CLOSE 4006,
 * SYS_GETDENTS64 4219. Fds are opaque slots; getdents fills
 * linux_dirent64 entries from POSIX readdir. */
enum { SIM_SYS_OPEN = 4005, SIM_SYS_CLOSE = 4006, SIM_SYS_GETDENTS64 = 4219 };
typedef struct { int used; DIR *dir; } sim_dirent_slot;
static sim_dirent_slot dir_slots[16];

long sc(long n, long a, long b, long c, long d, long e, long f) {
    (void)d; (void)e; (void)f;
    if (n == SIM_SYS_OPEN) {
        const char *path = (const char *)a;
        for (int i = 0; i < 16; ++i) {
            if (dir_slots[i].used) continue;
            DIR *dir = opendir(path);
            if (!dir) return -1;
            dir_slots[i].used = 1;
            dir_slots[i].dir = dir;
            return 100 + i;
        }
        return -1;
    }
    if (n == SIM_SYS_CLOSE) {
        int fd = (int)a;
        int i = fd - 100;
        if (i < 0 || i >= 16 || !dir_slots[i].used) return -1;
        closedir(dir_slots[i].dir);
        dir_slots[i].used = 0;
        dir_slots[i].dir = NULL;
        return 0;
    }
    if (n == SIM_SYS_GETDENTS64) {
        int fd = (int)a;
        int i = fd - 100;
        if (i < 0 || i >= 16 || !dir_slots[i].used) return -1;
        char *buf = (char *)b;
        long size = c;
        long used = 0;
        struct dirent *de;
        while ((de = readdir(dir_slots[i].dir)) != NULL) {
            unsigned name_len = (unsigned)strlen(de->d_name);
            unsigned reclen = (19u + name_len + 1u + 7u) & ~7u;
            if (used + (long)reclen > size) {
                /* Not expected for a 2048-byte buffer and short names;
                 * rewind is unavailable via readdir, so report what we
                 * have and let the caller loop again — the entry is
                 * lost, which the browser tolerates (max 64 carts). */
                break;
            }
            char *dst = buf + used;
            memset(dst, 0, reclen);
            dst[16] = (char)(reclen & 0xff);
            dst[17] = (char)((reclen >> 8) & 0xff);
            dst[18] = (char)de->d_type;
            memcpy(dst + 19, de->d_name, name_len + 1);
            used += (long)reclen;
        }
        return used;
    }
    (void)c;
    return -1;
}
