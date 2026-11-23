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
 *  - q2_read_input(): scripted raw Q2 events (Q2 Pod card-app key codes,
 *    wheel travel, touch down/move/up) are fed to the REAL mapper in
 *    src/q2_input.c, exactly as src/q2_platform.c feeds the socket and
 *    GPIO events. Q2_SIM_FULL=1 (default) simulates all five buttons
 *    (gameview firmware or grabbed GPIO keys); Q2_SIM_FULL=0 simulates
 *    Q2 Pod 1.8's fallback. Events come from a script file
 *    (Q2_SIM_INPUT), scheduled by wall-clock time since q2_open().
 *  - q2_present_canvas(): the native 375x320 UI, stored with the same
 *    rotation as the device and dumped to PNG like game frames.
 *  - q2_millis(): real CLOCK_MONOTONIC time, NOT the +34 ms fake time
 *    in tests/q2_fake08_stub.c — so frame pacing is genuinely measured.
 *  - ALSA: replaced by a WAV capture sink (Q2_SIM_OUTDIR/audio.wav,
 *    22050 Hz stereo s16). It never blocks, like the device's
 *    non-blocking ALSA write, and records peak/RMS/frames written.
 *  - Framebuffer: a 375x320 in-memory buffer dumped to PNG on every
 *    present, with a per-frame timestamp and hash for pacing analysis.
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

#include "q2_input.h"

/* --- state ------------------------------------------------------------ */
static q2_pixel *fb_mem;
static uint32_t canvas_mem[Q2_WIDTH * Q2_HEIGHT];
static int quit_flag, sim_full = 1;
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
enum { EV_WHEEL, EV_KEY_DOWN, EV_KEY_UP, EV_TOUCH, EV_QUIT };
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
/* Physical Q2 buttons, as Q2 Pod's card-app codes. */
static int key_code(const char *name) {
    if (!strcmp(name, "RETURN") || !strcmp(name, "BACK")) return Q2_KEY_RETURN;
    if (!strcmp(name, "PLAY")) return Q2_KEY_PLAY;
    if (!strcmp(name, "PREV")) return Q2_KEY_PREVIOUS;
    if (!strcmp(name, "NEXT")) return Q2_KEY_NEXT;
    if (!strcmp(name, "CENTRE") || !strcmp(name, "CENTER")) return Q2_KEY_CENTRE;
    return -1;
}

static void add_event(sim_event ev) {
    events = realloc(events, (event_count + 1) * sizeof *events);
    events[event_count++] = ev;
}

/* Script lines (ms since q2_open):
 *   <ms> wheel <travel>                clockwise positive, 200 a turn
 *   <ms> key <BUTTON> down|up|press    RETURN PLAY PREV NEXT CENTRE (BACK = RETURN)
 *   <ms> touch <x> <y> down|move|up
 *   <ms> quit
 * "press" is down then up 60 ms later. */
static void load_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "q2-sim: no input script %s\n", path); return; }
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
            if (sscanf(p, "%*u %*s %31s %15s", name, dir) == 2 && key_code(name) >= 0) {
                ev.a = key_code(name);
                if (!strcmp(dir, "up")) ev.kind = EV_KEY_UP;
                else ev.kind = EV_KEY_DOWN;
                if (!strcmp(dir, "press")) {
                    sim_event up = ev;
                    up.ms = ms + 60;
                    up.kind = EV_KEY_UP;
                    add_event(up);
                }
            }
        } else if (!strcmp(word, "touch")) {
            int x = 0, y = 0;
            if (sscanf(p, "%*u %*s %d %d %15s", &x, &y, dir) == 3) {
                /* The touch action ('d', 'm' or 'u') rides above the kind. */
                char action = dir[0] == 'd' ? 'd' : dir[0] == 'm' ? 'm' : 'u';
                ev.kind = EV_TOUCH | (action << 8);
                ev.a = x;
                ev.b = y;
            }
        } else if (!strcmp(word, "quit")) {
            ev.kind = EV_QUIT;
        }
        if (ev.kind < 0) continue;
        add_event(ev);
    }
    fclose(f);
    /* insertion sort by ms, stable for equal times */
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

static void log_frame(uint64_t h);
/* Same blend as src/q2_platform.c's plot_overlay (stride = Q2_HEIGHT here). */
static void plot_overlay(void *ctx, int x, int y, uint32_t rgb, unsigned alpha) {
    if ((unsigned)x >= Q2_WIDTH || (unsigned)y >= Q2_HEIGHT) return;
    q2_pixel *p = (q2_pixel *)ctx + x * Q2_HEIGHT + (Q2_HEIGHT - 1 - y);
    uint32_t under = *p;
    if (alpha > 256) alpha = 256;
    uint32_t rb = ((rgb & 0xff00ffu) * alpha + (under & 0xff00ffu) * (256 - alpha)) >> 8;
    uint32_t g = ((rgb & 0x00ff00u) * alpha + (under & 0x00ff00u) * (256 - alpha)) >> 8;
    *p = 0xff000000u | (rb & 0xff00ffu) | (g & 0x00ff00u);
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
    if (map) q2_overlay_paint(plot_overlay, fb_mem);
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a(pixels, 8192, h);
    if (map) h = fnv1a(map, 16, h);
    log_frame(h);
}

static void log_frame(uint64_t h) {
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

uint32_t *q2_canvas(void) { return canvas_mem; }
void q2_present_canvas(void) {
    if (!fb_mem) return;
    for (unsigned x = 0; x < Q2_WIDTH; ++x)
        for (unsigned y = 0; y < Q2_HEIGHT; ++y)
            fb_mem[x * Q2_HEIGHT + (Q2_HEIGHT - 1 - y)] = 0xff000000u | canvas_mem[y * Q2_WIDTH + x];
    log_frame(fnv1a((const uint8_t *)canvas_mem, sizeof canvas_mem, 1469598103934665603ULL));
}

void q2_present_fake08(const uint8_t *pixels, const uint8_t screen_map[16], const uint32_t colors[144]) {
    if (!screen_map) return;
    present_mapped(pixels, screen_map, colors);
}

/* --- open / close / input ---------------------------------------------- */
int q2_open(void) {
    q2_input_reset();
    const char *full = getenv("Q2_SIM_FULL");
    sim_full = !(full && full[0] == '0');
    q2_input_set_full(sim_full);
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
    while (event_next < event_count && events[event_next].ms <= now) {
        sim_event ev = events[event_next++];
        ++input_events_applied;
        switch (ev.kind & 0xff) {
        case EV_WHEEL: q2_input_wheel(ev.a, now); break;
        case EV_KEY_DOWN: q2_input_key(ev.a, 1, now); break;
        case EV_KEY_UP: q2_input_key(ev.a, 0, now); break;
        case EV_TOUCH: q2_input_touch((char)(ev.kind >> 8), ev.a, ev.b, now); break;
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
    if (quit_flag) q2_input_quit();
    q2_input in = q2_input_take(now);
    if (in.quit && !quit_flag) {
        quit_flag = 1;
        strcpy(exit_reason, sim_full ? "return-centre" : "double-back");
    }
    return in;
}

/* The device-only argument hook; game mode switches the real mapper. */
void q2_set_args(int argc, char **argv) { (void)argc; (void)argv; }
void q2_set_game_mode(int on) { q2_input_set_game(on); }

void q2_debug_stage(unsigned stage, uint32_t rgb, const char *what) { (void)stage; (void)rgb; (void)what; }
