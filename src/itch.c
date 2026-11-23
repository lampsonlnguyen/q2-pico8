// SPDX-License-Identifier: GPL-2.0-or-later
// Helper process runner from Pocket Boy (github.com/SlayTheEarth/q2-boy).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "itch.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

bool itch_start(ItchJob *job, const char *helper, char *const arguments[]) {
    int fds[2];
    if (job->pid || pipe2(fds, O_CLOEXEC)) return false;
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK)) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    pid_t parent = getpid();
    pid_t pid = fork();
    if (!pid) {
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) _exit(127);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        execve(helper, arguments, environ);
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return false;
    }
    job->pid = pid;
    job->fd = fds[0];
    job->length = 0;
    job->result[0] = 0;
    return true;
}

static void read_output(ItchJob *job) {
    char discard[512];
    for (;;) {
        size_t space = sizeof job->result - job->length - 1;
        ssize_t n = read(job->fd, space ? job->result + job->length : discard,
                         space ? space : sizeof discard);
        if (n <= 0) break;
        if (space) job->length += (size_t)n;
    }
    job->result[job->length] = 0;
}

bool itch_finished(ItchJob *job, bool *success) {
    if (!job->pid) return false;
    read_output(job);
    int status;
    pid_t ended = waitpid(job->pid, &status, WNOHANG);
    if (!ended || (ended < 0 && errno == EINTR)) return false;
    read_output(job);
    close(job->fd);
    job->pid = 0;
    *success = ended > 0 && WIFEXITED(status) && !WEXITSTATUS(status);
    return true;
}

void itch_stop(ItchJob *job) {
    if (!job->pid) return;
    kill(job->pid, SIGTERM);
    while (waitpid(job->pid, NULL, 0) < 0 && errno == EINTR) {}
    close(job->fd);
    job->pid = 0;
}

int itch_catalog(char *result, ItchGame games[ITCH_LIMIT], bool *more) {
    *more = false;
    int count = 0;
    char *save = NULL;
    char *line = strtok_r(result, "\n", &save);
    if (!line || (strcmp(line, "P 0") && strcmp(line, "P 1"))) return 0;
    *more = line[2] == '1';
    for (line = strtok_r(NULL, "\n", &save); line && count < ITCH_LIMIT;
         line = strtok_r(NULL, "\n", &save)) {
        ItchGame entry = {0};
        char kind[8] = "";
        if (sscanf(line, "%" SCNu64 "\t%95[^\t]\t%45[^\t]\t%26[^\t]\t%249[^\t]\t%7s",
                   &entry.id, entry.title, entry.creator, entry.price, entry.url, kind) >= 5 &&
            entry.id) {
            entry.web = !strcmp(kind, "web");
            games[count++] = entry;
        }
    }
    return count;
}

int itch_qr(char *result, unsigned char grid[ITCH_QR_LIMIT][ITCH_QR_LIMIT]) {
    const char *newline = strchr(result, '\n');
    size_t size = newline ? (size_t)(newline - result) : 0;
    if (size < 21 || size > ITCH_QR_LIMIT) return 0;
    for (size_t y = 0; y < size; ++y) {
        for (size_t x = 0; x < size; ++x) {
            if (*result != '0' && *result != '1') return 0;
            grid[y][x] = *result++ == '1';
        }
        if (*result++ != '\n') return 0;
    }
    return *result ? 0 : (int)size;
}
