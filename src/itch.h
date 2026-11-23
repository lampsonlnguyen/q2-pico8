// SPDX-License-Identifier: GPL-2.0-or-later
// Helper process runner from Pocket Boy (github.com/SlayTheEarth/q2-boy).
#ifndef Q2_PICO8_ITCH_H
#define Q2_PICO8_ITCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ITCH_LIMIT 80
#define ITCH_RESULT 36000
#define ITCH_QR_LIMIT 73

typedef struct {
    uint64_t id;
    char title[96], creator[46], price[27], url[250];
    bool web; /* only a PICO-8 web export: converted when downloaded */
} ItchGame;

typedef struct {
    pid_t pid;
    int fd;
    size_t length;
    char result[ITCH_RESULT];
} ItchJob;

bool itch_start(ItchJob *job, const char *helper, char *const arguments[]);
bool itch_finished(ItchJob *job, bool *success);
void itch_stop(ItchJob *job);
int itch_catalog(char *result, ItchGame games[ITCH_LIMIT], bool *more);
int itch_qr(char *result, unsigned char grid[ITCH_QR_LIMIT][ITCH_QR_LIMIT]);

#ifdef __cplusplus
}
#endif
#endif
