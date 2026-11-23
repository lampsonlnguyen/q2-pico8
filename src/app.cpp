/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace app {
namespace {
std::string exe_dir;
bool tail_is(const std::string &s, const char *tail) {
    size_t n = strlen(tail);
    if (s.size() <= n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != tail[i]) return false;
    }
    return true;
}
}

const std::string &data_dir() {
    static std::string dir;
    if (dir.empty()) {
        const char *env = getenv("Q2_PICO8_DATA");
        dir = env && *env ? env : "/mnt/mmc/.pico8";
    }
    return dir;
}
std::string cart_dir() { return data_dir() + "/carts"; }
std::string art_dir() { return data_dir() + "/covers"; }

void set_exe(const char *argv0) {
    char buf[512];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    std::string path = n > 0 ? std::string(buf, (size_t)n) : std::string(argv0 ? argv0 : "");
    size_t slash = path.rfind('/');
    exe_dir = slash == std::string::npos ? "." : path.substr(0, slash);
}

std::string helper() {
    const char *env = getenv("Q2_PICO8_ITCH");
    if (env && *env) return env;
    return (exe_dir.empty() ? data_dir() : exe_dir) + "/q2-pico8-itch";
}

bool is_cart(const std::string &file) {
    return !file.empty() && file[0] != '.' && (tail_is(file, ".p8") || tail_is(file, ".p8.png"));
}

std::string title_of(const std::string &file) {
    std::string t = file;
    if (tail_is(t, ".p8.png")) t.resize(t.size() - 7);
    else if (tail_is(t, ".p8")) t.resize(t.size() - 3);
    size_t tag = t.rfind(" [itch ");
    if (tag != std::string::npos && !t.empty() && t.back() == ']') t.resize(tag);
    for (char &c : t) if (c == '_') c = ' ';
    return t.empty() ? file : t;
}

std::vector<std::string> list_carts() {
    std::vector<std::string> out;
    DIR *dir = opendir(cart_dir().c_str());
    if (!dir) return out;
    while (struct dirent *e = readdir(dir)) {
        std::string name = e->d_name;
        if (!is_cart(name)) continue;
        struct stat st;
        if (stat((cart_dir() + "/" + name).c_str(), &st) == 0 && S_ISREG(st.st_mode))
            out.push_back(name);
    }
    closedir(dir);
    return out;
}

std::vector<std::string> read_lines(const std::string &path, size_t limit) {
    std::vector<std::string> out;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return out;
    char line[512];
    while (out.size() < limit && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n) out.emplace_back(line);
    }
    fclose(f);
    return out;
}

bool write_lines(const std::string &path, const std::vector<std::string> &lines) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) return false;
    for (const auto &l : lines) fprintf(f, "%s\n", l.c_str());
    bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    return ok && rename(tmp.c_str(), path.c_str()) == 0;
}

}
