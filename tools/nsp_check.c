// runtime/services/nsp.c against what tools/zaimport (LibHac) made from the same packages: the whole RomFS byte for
// byte against its romfs.bin, random unaligned reads, and the exefs files. Nothing of the game is printed (offsets and
// sizes only), nor any key.
//   cc -O2 -o nsp_check tools/nsp_check.c runtime/services/nsp.c
//   nsp_check <prod.keys> <title.keys|-> <romfs.bin> <exefs dir> <out dir> [--quick] <package.nsp>...
// --quick: the random reads only (the full pass reads all ~6 GB of both).
#include "../runtime/services/nsp.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) {
        for (long i = 0; i < n; i++) if (!b[i]) b[i] = '\n';  // (as runtime/services/nsp_list.c nsp_slurp_fd)
        b[n] = 0;
    }
    fclose(f);
    return b;
}
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static ssize_t ref_read(int fd, void *b, size_t n, off_t off) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = pread(fd, (char *)b + got, n - got, off + (off_t)got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

// --list <prod.keys> <title.keys|-> <package>...: what each package is (nsp_classify, with the keys and without), its
// NCAs and tickets, and whether title.keys has a title key for each rights id (yes / no: the key is never printed)
static int list_mode(int argc, char **argv) {
    static const char *const kinds[] = {"not a package", "xci", "unknown", "base game", "update", "DLC", "another game"};
    char *pk = slurp(argv[2]), *tk = strcmp(argv[3], "-") ? slurp(argv[3]) : NULL;
    for (char *p = tk; p && *p; p++) if (*p >= 'A' && *p <= 'F') *p += 32;
    for (int i = 4; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        const char *nm = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        if (fd < 0) { printf("%s: can't open\n", nm); continue; }
        char rep[4096];
        uint64_t t1 = 0, t2 = 0;
        int k1 = nsp_classify(fd, pk, &t1, rep, sizeof rep), k2 = nsp_classify(fd, NULL, &t2, NULL, 0);
        printf("%s\n  with keys: %s (%016llx); without keys: %s (%016llx)\n", nm, kinds[k1], (unsigned long long)t1,
               kinds[k2], (unsigned long long)t2);
        for (char *l = rep, *e; *l; l = e + 1) {
            e = strchr(l, '\n');
            if (!e) break;
            *e = 0;
            char *r = strstr(l, "rights id ");
            int has = r && strlen(r + 10) == 32 && tk && strstr(tk, r + 10);
            printf("    %s%s\n", l, r && strlen(r + 10) == 32 ? (has ? " - in title.keys: yes" : " - in title.keys: no") : "");
        }
        close(fd);
    }
    if (pk) memset(pk, 0, strlen(pk));
    if (tk) memset(tk, 0, strlen(tk));
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 4 && !strcmp(argv[1], "--list")) return list_mode(argc, argv);
    if (argc < 7) {
        fprintf(stderr, "usage: nsp_check <prod.keys> <title.keys|-> <romfs.bin> <exefs dir> <out dir> [--quick] <nsp>...\n");
        return 2;
    }
    char *pk = slurp(argv[1]), *tk = strcmp(argv[2], "-") ? slurp(argv[2]) : NULL;
    if (!pk) { fprintf(stderr, "can't read prod.keys\n"); return 2; }
    int first = 6, quick = 0;
    if (!strcmp(argv[6], "--quick")) quick = 1, first = 7;
    int n = argc - first, fds[16];
    const char *names[16];
    for (int i = 0; i < n && i < 16; i++) {
        fds[i] = open(argv[first + i], O_RDONLY);
        const char *s = strrchr(argv[first + i], '/');
        names[i] = s ? s + 1 : argv[first + i];
        if (fds[i] < 0) { fprintf(stderr, "can't open %s\n", argv[first + i]); return 2; }
    }
    char why[512] = "";
    double t0 = now();
    NspGame *g = nsp_open(fds, names, n, pk, tk, why, sizeof why);
    memset(pk, 0, strlen(pk));
    if (tk) memset(tk, 0, strlen(tk));
    if (!g) { printf("FAIL open: %s\n", why); return 1; }
    printf("open: %s (%.3f s)\n", nsp_describe(g), now() - t0);
    int ref = open(argv[3], O_RDONLY);
    struct stat sb;
    if (ref < 0 || fstat(ref, &sb)) { printf("FAIL: no reference romfs.bin\n"); return 1; }
    uint64_t size = nsp_romfs_size(g);
    printf("romfs: %llu bytes, reference %llu: %s\n", (unsigned long long)size, (unsigned long long)sb.st_size,
           size == (uint64_t)sb.st_size ? "same size" : "DIFFERENT SIZE");
    int bad = size != (uint64_t)sb.st_size;

    // random reads: any offset, any length (alignment edges, ranges across patch entries), and past the end
    size_t cap = 4 << 20;
    unsigned char *a = malloc(cap), *b = malloc(cap);
    srand(1234);
    int rbad = 0;
    t0 = now();
    for (int k = 0; k < 3000 && size; k++) {
        uint64_t off = ((uint64_t)rand() << 31 ^ (uint64_t)rand()) % size;
        size_t len = k % 3 == 0 ? (size_t)(rand() % 40) + 1 : k % 3 == 1 ? (size_t)(rand() % 70000) + 1 : (size_t)(rand() % cap) + 1;
        if (k == 2999) off = size - 5, len = 100;
        ssize_t x = nsp_romfs_read(g, a, len, off), y = ref_read(ref, b, len, (off_t)off);
        if (x != y || (x > 0 && memcmp(a, b, (size_t)x))) {
            if (rbad++ < 8) printf("  random read MISMATCH at %llu + %zu (got %zd, reference %zd)\n",
                                   (unsigned long long)off, len, x, y);
        }
    }
    printf("random reads: %s (%.2f s)\n", rbad ? "MISMATCHES" : "3000 identical", now() - t0);
    bad |= rbad != 0;

    if (!quick) {
        size_t chunk = 4 << 20;
        uint64_t mism = 0, firstbad = (uint64_t)-1;
        t0 = now();
        for (uint64_t off = 0; off < size; off += chunk) {
            size_t len = size - off < chunk ? (size_t)(size - off) : chunk;
            ssize_t x = nsp_romfs_read(g, a, len, off), y = ref_read(ref, b, len, (off_t)off);
            if (x != (ssize_t)len || y != (ssize_t)len || memcmp(a, b, len)) {
                if (firstbad == (uint64_t)-1) {
                    size_t i = 0;
                    if (x == (ssize_t)len && y == (ssize_t)len) while (i < len && a[i] == b[i]) i++;
                    firstbad = off + i;
                }
                mism++;
            }
            if ((off / chunk) % 256 == 0) {
                fprintf(stderr, "\r  %5.1f%%", 100.0 * (double)off / (double)size);
                fflush(stderr);
            }
        }
        double dt = now() - t0;
        fprintf(stderr, "\r");
        if (mism) printf("full pass: %llu chunks of 4 MB differ, first byte %llu\n", (unsigned long long)mism,
                         (unsigned long long)firstbad);
        else printf("full pass: all %llu bytes identical (%.1f s, %.0f MB/s both read)\n", (unsigned long long)size, dt,
                    (double)size / dt / 1e6);
        bad |= mism != 0;
    }

    // exefs: every file of the reference, the same bytes
    if (!nsp_exefs_extract(g, argv[5], why, sizeof why)) { printf("FAIL exefs: %s\n", why); bad = 1; }
    else {
        static const char *const files[] = {"main", "main.npdm", "rtld", "sdk"};
        for (int i = 0; i < 4; i++) {
            char p1[1200], p2[1200];
            snprintf(p1, sizeof p1, "%s/%s", argv[4], files[i]);
            snprintf(p2, sizeof p2, "%s/%s", argv[5], files[i]);
            FILE *f1 = fopen(p1, "rb"), *f2 = fopen(p2, "rb");
            int same = f1 && f2;
            while (same) {
                size_t r1 = fread(a, 1, cap, f1), r2 = fread(b, 1, cap, f2);
                if (r1 != r2 || memcmp(a, b, r1)) same = 0;
                if (!r1) break;
            }
            printf("exefs/%s: %s\n", files[i], same ? "identical" : "DIFFERENT");
            bad |= !same;
            if (f1) fclose(f1);
            if (f2) fclose(f2);
        }
    }
    for (int i = 0; i < nsp_aoc_count(g); i++) {
        unsigned char h[8] = {0};  // (a RomFS starts with its header size, 0x50: a wrong key gives noise there)
        int ok = nsp_aoc_read(g, i, h, 8, 0) == 8 && h[0] == 0x50 && !h[1] && !h[2] && !h[3] && !h[4];
        printf("DLC %016llx: %llu bytes, RomFS header %s\n", (unsigned long long)nsp_aoc_title(g, i),
               (unsigned long long)nsp_aoc_size(g, i), ok ? "ok" : "WRONG (bad key?)");
        bad |= !ok;
    }
    nsp_close(g);
    printf(bad ? "RESULT: FAILED\n" : "RESULT: OK\n");
    return bad;
}
