// The game folder's packages (runtime/services/nsp.h): packages.txt and the keys copied beside it, opened for the game
// (runtime/services/fs.c) and for the start screen's check (runtime/ui/launcher.mm).
#include "nsp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../platform.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

int nsp_open_source(const char *src, bool keep) {
#ifdef __ANDROID__
    if (!strncmp(src, "content://", 10)) return plat_android_open_uri(src, keep);
#endif
    (void)keep;
    return open(src, O_RDONLY | O_BINARY);
}

char *nsp_slurp_fd(int fd, size_t max) {
    struct stat sb;
    if (fd < 0 || fstat(fd, &sb) || sb.st_size < 0 || (uint64_t)sb.st_size > max) return NULL;
    char *b = malloc((size_t)sb.st_size + 1);
    size_t got = 0;
    while (b && got < (size_t)sb.st_size) {
        ssize_t r = pread(fd, b + got, (size_t)sb.st_size - got, (off_t)got);
        if (r <= 0) { free(b); return NULL; }
        got += (size_t)r;
    }
    if (b) b[got] = 0;
    return b;
}

static char *slurp_path(const char *path) {
    int fd = open(path, O_RDONLY | O_BINARY);
    char *t = nsp_slurp_fd(fd, 1 << 20);
    if (fd >= 0) close(fd);
    return t;
}

// the last part of a path or URI, decoded ("…/document/primary%3ASwitch%2FZA.nsp" -> "ZA.nsp"): only for messages
void nsp_source_name(const char *src, char *out, size_t n) {
    char dec[1024];
    size_t k = 0;
    for (const char *p = src; *p && k + 1 < sizeof dec; p++) {
        int h1, h2;
        if (*p == '%' && sscanf(p + 1, "%1x%1x", &h1, &h2) == 2) dec[k++] = (char)(h1 << 4 | h2), p += 2;
        else dec[k++] = *p;
    }
    dec[k] = 0;
    const char *leaf = dec;
    for (const char *p = dec; *p; p++) if (*p == '/' || *p == ':') leaf = p + 1;
    snprintf(out, n, "%s", *leaf ? leaf : "a package");
}

NspGame *nsp_open_game_dir(const char *game_dir, char *why, size_t wn) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", game_dir, NSP_LIST);
    char *list = slurp_path(path);
    if (!list) {
        snprintf(why, wn, "%s can't be read", NSP_LIST);
        return NULL;
    }
    int fds[16], n = 0;
    char names[16][160];
    const char *np[16];
    NspGame *g = NULL;
    for (char *l = list, *e; *l && n < 16; l = *e ? e + 1 : e) {
        e = l + strcspn(l, "\r\n");
        char c = *e;
        *e = 0;
        if (*l && *l != '#') {
            nsp_source_name(l, names[n], sizeof names[n]);
            np[n] = names[n];
            fds[n] = nsp_open_source(l, false);
            if (fds[n] < 0) {
                snprintf(why, wn, "%s can't be opened (%s): moved, deleted, or the app's access to it was removed - "
                         "choose the game files again", names[n], strerror(errno));
                *e = c;
                goto out;
            }
            n++;
        }
        *e = c;
    }
    snprintf(path, sizeof path, "%s/keys/prod.keys", game_dir);
    char *pk = slurp_path(path);
    snprintf(path, sizeof path, "%s/keys/title.keys", game_dir);
    char *tk = slurp_path(path);
    if (!pk) snprintf(why, wn, "keys/prod.keys is missing: choose the game files again with your prod.keys");
    else if (!n) snprintf(why, wn, "%s lists no packages", NSP_LIST);
    else g = nsp_open(fds, np, n, pk, tk, why, wn);
    if (pk) memset(pk, 0, strlen(pk)), free(pk);
    if (tk) memset(tk, 0, strlen(tk)), free(tk);
    if (g) nsp_adopt_fds(g, fds, n), n = 0;
out:
    for (int i = 0; i < n; i++) close(fds[i]);
    free(list);
    return g;
}
