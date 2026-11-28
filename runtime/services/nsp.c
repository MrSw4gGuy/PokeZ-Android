// Pokémon Legends Z-A's data read straight from the player's .nsp packages (runtime/services/nsp.h).
//
// The layers, outside in (what tools/zaimport does through LibHac, kept to what Z-A's packages use):
//  - the .nsp is a PFS0: a file table, then the files - the NCAs and the tickets (.tik);
//  - an NCA's 0xC00-byte header is AES-128-XTS with header_key (NCA3: sector i at i * 0x200, the tweak the sector
//    number big-endian); it gives the content type, the title id, up to 4 sections and their FS headers;
//  - a section's bytes are AES-128-CTR: the counter's upper half from its FS header, the lower half the byte offset in
//    the NCA / 16. The key: the title key from the ticket (titlekek_<gen>) when the NCA has a rights id, else the
//    key area's third key (key_area_key_<kind>_<gen>);
//  - the RomFS is the last level of the section's IVFC hash tree (hashes not checked here: zaimport checks them all
//    on import, and the game reads ~1.5 MB at a time where a hash walk would double every read);
//  - the update's data section is a patch (BKTR): an indirect table maps the patched section, range by range, to the
//    base's section or to the update's own bytes, whose counter generation changes by range (the AesCtrEx table).
//    Both tables are bucket trees (LibHac's BucketTree: one 16 KB node of set offsets, then 16 KB sets of entries).
#include "nsp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include "../platform/win_compat.h"
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sys/auxv.h>
#ifndef HWCAP_AES
#define HWCAP_AES (1 << 3)
#endif
#endif
#elif defined(__x86_64__) || defined(_M_X64)
#include <wmmintrin.h>
#endif

#define ZA_BASE 0x0100F43008C44000ull  // (update: +0x800, DLC: +0x1000 + n)
#define NCA_HDR 0xC00
#define BKTR_NODE 0x4000

static void say(char *why, size_t wn, const char *fmt, ...) {
    if (!why || !wn) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, wn, fmt, ap);
    va_end(ap);
}
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

// ------------------------------------------------------------------ AES-128
// The tables are made at start (the S-box from GF(2^8) inverses, as FIPS-197 defines it) rather than typed in, and
// checked against FIPS-197's example before any key is used.
static uint8_t SBOX[256], INV_SBOX[256];
typedef struct { uint8_t rk[176]; } Aes;  // (the 11 round keys, in state byte order)

static uint8_t xt(uint8_t a) { return (uint8_t)(a << 1 ^ (a & 0x80 ? 0x1B : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    for (; b; b >>= 1, a = xt(a)) if (b & 1) r ^= a;
    return r;
}
static uint8_t rotl8(uint8_t x, int s) { return (uint8_t)(x << s | x >> (8 - s)); }
static void aes_tables(void) {
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ p << 1 ^ (p & 0x80 ? 0x1B : 0));  // (p * 3)
        q ^= q << 1, q ^= q << 2, q ^= q << 4;               // (q / 3)
        if (q & 0x80) q ^= 0x09;
        SBOX[p] = (uint8_t)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63);
    } while (p != 1);
    SBOX[0] = 0x63;
    for (int i = 0; i < 256; i++) INV_SBOX[SBOX[i]] = (uint8_t)i;
}

static void aes_key(Aes *a, const uint8_t key[16]) {
    static const uint8_t rcon[10] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};
    memcpy(a->rk, key, 16);
    for (int i = 4; i < 44; i++) {
        uint8_t t[4];
        memcpy(t, a->rk + 4 * (i - 1), 4);
        if (i % 4 == 0) {
            uint8_t t0 = t[0];
            t[0] = SBOX[t[1]] ^ rcon[i / 4 - 1], t[1] = SBOX[t[2]], t[2] = SBOX[t[3]], t[3] = SBOX[t0];
        }
        for (int k = 0; k < 4; k++) a->rk[4 * i + k] = a->rk[4 * (i - 4) + k] ^ t[k];
    }
}

static void aes_enc_c(const Aes *a, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ a->rk[i];
    for (int r = 1; r <= 10; r++) {
        for (int c = 0; c < 4; c++)  // (SubBytes + ShiftRows: row k moves k columns left)
            for (int k = 0; k < 4; k++) t[k + 4 * c] = SBOX[s[k + 4 * ((c + k) & 3)]];
        if (r < 10)
            for (int c = 0; c < 4; c++) {
                uint8_t *x = t + 4 * c, a0 = x[0], a1 = x[1], a2 = x[2], a3 = x[3];
                x[0] = xt(a0) ^ xt(a1) ^ a1 ^ a2 ^ a3;
                x[1] = a0 ^ xt(a1) ^ xt(a2) ^ a2 ^ a3;
                x[2] = a0 ^ a1 ^ xt(a2) ^ xt(a3) ^ a3;
                x[3] = xt(a0) ^ a0 ^ a1 ^ a2 ^ xt(a3);
            }
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ a->rk[16 * r + i];
    }
    memcpy(out, s, 16);
}

static void aes_dec_c(const Aes *a, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ a->rk[160 + i];
    for (int r = 9; r >= 0; r--) {
        for (int c = 0; c < 4; c++)  // (InvShiftRows + InvSubBytes)
            for (int k = 0; k < 4; k++) t[k + 4 * ((c + k) & 3)] = INV_SBOX[s[k + 4 * c]];
        for (int i = 0; i < 16; i++) t[i] ^= a->rk[16 * r + i];
        if (r > 0)
            for (int c = 0; c < 4; c++) {
                uint8_t *x = t + 4 * c, a0 = x[0], a1 = x[1], a2 = x[2], a3 = x[3];
                x[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
                x[1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
                x[2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
                x[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
            }
        memcpy(s, t, 16);
    }
    memcpy(out, s, 16);
}

// CTR over whole blocks: buf ^= E(hi | big-endian(blk + j)). The hot path (every byte the game reads): with the
// CPU's AES instructions, 4 blocks at a time so the AES unit's pipeline stays full (the C one is ~30x slower).
static void ctr_c(const Aes *a, const uint8_t hi[8], uint64_t blk, uint8_t *buf, size_t nblk) {
    uint8_t c[16], k[16];
    memcpy(c, hi, 8);
    for (size_t j = 0; j < nblk; j++, buf += 16) {
        uint64_t v = blk + j;
        for (int i = 0; i < 8; i++) c[15 - i] = (uint8_t)(v >> 8 * i);
        aes_enc_c(a, c, k);
        for (int i = 0; i < 16; i++) buf[i] ^= k[i];
    }
}

#if defined(__aarch64__)
__attribute__((target("aes"))) static void ctr_arm(const Aes *a, const uint8_t hi[8], uint64_t blk, uint8_t *buf,
                                                   size_t nblk) {
    uint8x16_t rk[11];
    for (int i = 0; i < 11; i++) rk[i] = vld1q_u8(a->rk + 16 * i);
    uint64_t h;
    memcpy(&h, hi, 8);
    size_t j = 0;
    for (; j + 4 <= nblk; j += 4, buf += 64) {
        uint8x16_t s[4];
        for (int q = 0; q < 4; q++) {
            uint64_t c[2] = {h, __builtin_bswap64(blk + j + q)};
            s[q] = vld1q_u8((const uint8_t *)c);
        }
        for (int r = 0; r < 9; r++)
            for (int q = 0; q < 4; q++) s[q] = vaesmcq_u8(vaeseq_u8(s[q], rk[r]));
        for (int q = 0; q < 4; q++) {
            s[q] = veorq_u8(vaeseq_u8(s[q], rk[9]), rk[10]);
            vst1q_u8(buf + 16 * q, veorq_u8(vld1q_u8(buf + 16 * q), s[q]));
        }
    }
    for (; j < nblk; j++, buf += 16) {
        uint64_t c[2] = {h, __builtin_bswap64(blk + j)};
        uint8x16_t s = vld1q_u8((const uint8_t *)c);
        for (int r = 0; r < 9; r++) s = vaesmcq_u8(vaeseq_u8(s, rk[r]));
        s = veorq_u8(vaeseq_u8(s, rk[9]), rk[10]);
        vst1q_u8(buf, veorq_u8(vld1q_u8(buf), s));
    }
}
static bool cpu_aes(void) {
#if defined(__APPLE__)
    return true;  // (every Apple arm64 CPU has it)
#elif defined(__linux__) || defined(__ANDROID__)
    return (getauxval(AT_HWCAP) & HWCAP_AES) != 0;
#else
    return false;
#endif
}
#define CTR_HW ctr_arm
#define CTR_HW_NAME "ARMv8"
#elif defined(__x86_64__) || defined(_M_X64)
__attribute__((target("aes,sse2"))) static void ctr_ni(const Aes *a, const uint8_t hi[8], uint64_t blk, uint8_t *buf,
                                                       size_t nblk) {
    __m128i rk[11];
    for (int i = 0; i < 11; i++) rk[i] = _mm_loadu_si128((const __m128i *)(a->rk + 16 * i));
    uint64_t h;
    memcpy(&h, hi, 8);
    size_t j = 0;
    for (; j + 4 <= nblk; j += 4, buf += 64) {
        __m128i s[4];
        for (int q = 0; q < 4; q++) {
            uint64_t c[2] = {h, __builtin_bswap64(blk + j + q)};
            s[q] = _mm_xor_si128(_mm_loadu_si128((const __m128i *)c), rk[0]);
        }
        for (int r = 1; r < 10; r++)
            for (int q = 0; q < 4; q++) s[q] = _mm_aesenc_si128(s[q], rk[r]);
        for (int q = 0; q < 4; q++) {
            s[q] = _mm_aesenclast_si128(s[q], rk[10]);
            __m128i *p = (__m128i *)(buf + 16 * q);
            _mm_storeu_si128(p, _mm_xor_si128(_mm_loadu_si128(p), s[q]));
        }
    }
    for (; j < nblk; j++, buf += 16) {
        uint64_t c[2] = {h, __builtin_bswap64(blk + j)};
        __m128i s = _mm_xor_si128(_mm_loadu_si128((const __m128i *)c), rk[0]);
        for (int r = 1; r < 10; r++) s = _mm_aesenc_si128(s, rk[r]);
        s = _mm_aesenclast_si128(s, rk[10]);
        _mm_storeu_si128((__m128i *)buf, _mm_xor_si128(_mm_loadu_si128((const __m128i *)buf), s));
    }
}
static bool cpu_aes(void) { return __builtin_cpu_supports("aes"); }
#define CTR_HW ctr_ni
#define CTR_HW_NAME "AES-NI"
#endif

typedef void (*CtrFn)(const Aes *, const uint8_t *, uint64_t, uint8_t *, size_t);
static CtrFn g_ctr = ctr_c;
static const char *g_ctr_name = "C";

// FIPS-197 appendix C.1, and the CPU's CTR against the C one: a wrong table or a broken instruction path would turn
// the whole game into noise, so it's caught here, by name, before anything is read
static bool aes_init(char *why, size_t wn) {
    static int done;  // (1 ok, -1 failed)
    if (done) {
        if (done < 0) say(why, wn, "the AES self-test failed");
        return done > 0;
    }
    aes_tables();
    static const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    static const uint8_t ct[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    uint8_t key[16], b[16], d[16];
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)i;
    Aes a;
    aes_key(&a, key);
    aes_enc_c(&a, pt, b);
    aes_dec_c(&a, b, d);
    if (memcmp(b, ct, 16) || memcmp(d, pt, 16)) {
        done = -1;
        say(why, wn, "the AES self-test failed");
        return false;
    }
#ifdef CTR_HW
    if (cpu_aes()) {
        uint8_t x[16 * 7], y[16 * 7], hi[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        for (size_t i = 0; i < sizeof x; i++) x[i] = y[i] = (uint8_t)(i * 7);
        ctr_c(&a, hi, 0xFFFFFFFFFFFFFFFDull, x, 7);  // (a lower half that wraps: the instruction path's carry)
        CTR_HW(&a, hi, 0xFFFFFFFFFFFFFFFDull, y, 7);
        if (!memcmp(x, y, sizeof x)) g_ctr = CTR_HW, g_ctr_name = CTR_HW_NAME;
    }
#endif
    done = 1;
    return true;
}
const char *nsp_aes_impl(void) { return g_ctr_name; }

// ------------------------------------------------------------------ keys
// prod.keys / title.keys: "name = hex" lines (Lockpick_RCM's format; other tools add spaces, CRLF or comments)
typedef struct { char name[64]; uint8_t v[32]; int n; } Key;
typedef struct { Key *k; int n; } Keys;

static void keys_parse(Keys *ks, const char *text) {
    if (!text) return;
    for (const char *l = text; *l;) {
        const char *e = l + strcspn(l, "\r\n");
        const char *eq = memchr(l, '=', (size_t)(e - l));
        if (eq) {
            const char *a = l, *b = eq;
            while (a < b && (*a == ' ' || *a == '\t')) a++;
            while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
            Key k = {{0}, {0}, 0};
            if (b - a > 0 && b - a < (long)sizeof k.name) {
                for (int i = 0; i < b - a; i++) {
                    char c = a[i];
                    k.name[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
                }
                const char *h = eq + 1;
                int nib = 0, val = 0;
                for (; h < e && k.n < 32; h++) {
                    int d = *h >= '0' && *h <= '9' ? *h - '0' : *h >= 'a' && *h <= 'f' ? *h - 'a' + 10
                          : *h >= 'A' && *h <= 'F' ? *h - 'A' + 10 : -1;
                    if (d < 0) { if (*h == ' ' || *h == '\t') continue; break; }
                    val = val << 4 | d;
                    if (++nib == 2) k.v[k.n++] = (uint8_t)val, nib = 0, val = 0;
                }
                if (k.n) {
                    Key *grown = realloc(ks->k, sizeof *ks->k * (size_t)(ks->n + 1));
                    if (grown) ks->k = grown, ks->k[ks->n++] = k;
                }
            }
        }
        l = *e ? e + 1 : e;
    }
}
static const uint8_t *key_get(const Keys *ks, const char *name, int len) {
    for (int i = ks->n - 1; i >= 0; i--)  // (the last line of a name wins, as LibHac reads it)
        if (!strcmp(ks->k[i].name, name) && ks->k[i].n == len) return ks->k[i].v;
    return NULL;
}
static void keys_free(Keys *ks) {
    if (ks->k) memset(ks->k, 0, sizeof *ks->k * (size_t)ks->n);  // (no key material left in freed memory)
    free(ks->k);
    ks->k = NULL, ks->n = 0;
}

// ------------------------------------------------------------------ reading the packages
static bool rd(int fd, void *dst, uint64_t n, uint64_t off) {
    uint8_t *p = dst;
    while (n) {
        ssize_t r = pread(fd, p, n > (1u << 30) ? (1u << 30) : (size_t)n, (off_t)off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r, off += (uint64_t)r, n -= (uint64_t)r;
    }
    return true;
}

typedef struct { uint64_t off, size; char name[64]; } PfsFile;

// a PFS0 (the .nsp itself, an exefs) at off in fd, read through rfn (plain or decrypted); its files' offsets made
// absolute in the same space
typedef bool (*ReadFn)(const void *ctx, void *dst, uint64_t n, uint64_t off);
static int pfs0_list(ReadFn rfn, const void *ctx, uint64_t off, PfsFile **out) {
    uint8_t h[16];
    *out = NULL;
    if (!rfn(ctx, h, 16, off) || memcmp(h, "PFS0", 4)) return -1;
    uint32_t n = le32(h + 4), strsz = le32(h + 8);
    if (n == 0 || n > 4096 || strsz > (1u << 20)) return -1;
    size_t tsz = 0x18 * (size_t)n + strsz;
    uint8_t *t = malloc(tsz + 1);
    PfsFile *f = calloc(n, sizeof *f);
    if (!t || !f || !rfn(ctx, t, tsz, off + 16)) { free(t); free(f); return -1; }
    t[tsz] = 0;
    uint64_t data = off + 16 + tsz;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = t + 0x18 * i;
        uint32_t so = le32(e + 16);
        f[i].off = data + le64(e), f[i].size = le64(e + 8);
        snprintf(f[i].name, sizeof f[i].name, "%s", so < strsz ? (const char *)t + 0x18 * n + so : "");
    }
    free(t);
    *out = f;
    return (int)n;
}
static bool plain_read(const void *ctx, void *dst, uint64_t n, uint64_t off) { return rd(*(const int *)ctx, dst, n, off); }
static bool ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return false;
    for (size_t i = 0; i < b; i++) {
        char c = s[a - b + i];
        if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != suf[i]) return false;
    }
    return true;
}

// ------------------------------------------------------------------ NCAs
typedef struct { uint64_t off, end; bool patched; } Bucket;  // (an entry's range, filled from the next entry)
typedef struct {
    int fd;
    uint64_t base;      // (the NCA's offset in its package)
    uint64_t size;
    uint8_t hdr[NCA_HDR];
    Aes key;            // (the section key: title key or key area key 2)
    bool keyed;
    char name[160];
} Nca;
typedef struct {
    const Nca *nca;
    uint64_t start, end;  // (from the NCA's start)
    uint8_t hi[8];        // (the counter's upper half)
    bool plain;           // (encryption type 1: none)
    const uint8_t *fsh;
} Section;

static bool section(const Nca *c, int i, Section *s) {
    const uint8_t *e = c->hdr + 0x240 + 0x10 * i;
    uint64_t a = (uint64_t)le32(e) * 0x200, b = (uint64_t)le32(e + 4) * 0x200;
    if (b <= a || b > c->size) return false;
    s->nca = c, s->start = a, s->end = b, s->fsh = c->hdr + 0x400 + 0x200 * i;
    for (int k = 0; k < 8; k++) s->hi[k] = s->fsh[0x147 - k];  // (the FS header's upper counter, a little-endian u64)
    s->plain = s->fsh[4] == 1;
    return true;
}

// n bytes at off (from the NCA's start) of a section, AES-CTR with that upper counter half (plain: as stored). The
// partial blocks at the ends go through a 16-byte block of their own; the rest is decrypted in place in dst.
static bool ctr_read(const Nca *c, const uint8_t hi[8], bool plain, uint8_t *dst, uint64_t n, uint64_t off) {
    if (plain) return rd(c->fd, dst, n, c->base + off);
    while (n) {
        uint64_t a = off & ~15ull;
        if (off != a || n < 16) {
            uint8_t blk[16];
            size_t skip = (size_t)(off - a), k = 16 - skip < n ? 16 - skip : (size_t)n;
            if (!rd(c->fd, blk, 16, c->base + a)) return false;  // (sections end on 0x200: a whole block is there)
            g_ctr(&c->key, hi, a >> 4, blk, 1);
            memcpy(dst, blk + skip, k);
            dst += k, off += k, n -= k;
            continue;
        }
        uint64_t m = n & ~15ull;
        if (!rd(c->fd, dst, m, c->base + off)) return false;
        g_ctr(&c->key, hi, off >> 4, dst, (size_t)(m >> 4));
        dst += m, off += m, n -= m;
    }
    return true;
}
static bool sect_read(const void *ctx, void *dst, uint64_t n, uint64_t off) {  // (ReadFn: off from the section's start)
    const Section *s = ctx;
    if (off + n > s->end - s->start) return false;
    return ctr_read(s->nca, s->hi, s->plain, dst, n, s->start + off);
}

// XTS-decrypt the header (NCA3: 6 sectors of 0x200, tweak = sector number big-endian)
static void xts_header(const uint8_t key[32], uint8_t *h) {
    Aes k1, k2;
    aes_key(&k1, key), aes_key(&k2, key + 16);
    for (int sct = 0; sct < NCA_HDR / 0x200; sct++) {
        uint8_t t[16] = {0}, x[16];
        t[15] = (uint8_t)sct;
        aes_enc_c(&k2, t, t);
        for (int j = 0; j < 0x200 / 16; j++) {
            uint8_t *b = h + 0x200 * sct + 16 * j;
            for (int i = 0; i < 16; i++) x[i] = b[i] ^ t[i];
            aes_dec_c(&k1, x, x);
            for (int i = 0; i < 16; i++) b[i] = x[i] ^ t[i];
            int carry = t[15] >> 7;
            for (int i = 15; i > 0; i--) t[i] = (uint8_t)(t[i] << 1 | t[i - 1] >> 7);
            t[0] = (uint8_t)(t[0] << 1 ^ (carry ? 0x87 : 0));
        }
    }
    memset(&k1, 0, sizeof k1), memset(&k2, 0, sizeof k2);
}

typedef struct { uint8_t rights[16], key[16]; } TitleKey;  // (the title key as the ticket has it: under titlekek)

static int nca_gen(const Nca *c) {
    int g = c->hdr[0x206] > c->hdr[0x220] ? c->hdr[0x206] : c->hdr[0x220];
    return g ? g - 1 : 0;
}
static bool rights_id(const Nca *c) {
    for (int i = 0; i < 16; i++) if (c->hdr[0x230 + i]) return true;
    return false;
}

// the section key: from a title key (rights id) or the key area; false and why when the keys don't have it
static bool nca_keyed(Nca *c, const Keys *ks, const TitleKey *tk, int ntk, char *why, size_t wn) {
    char nm[64];
    int gen = nca_gen(c);
    uint8_t k[16];
    if (rights_id(c)) {
        const uint8_t *enc = NULL;
        for (int i = 0; i < ntk && !enc; i++) if (!memcmp(tk[i].rights, c->hdr + 0x230, 16)) enc = tk[i].key;
        char rid[33];
        for (int i = 0; i < 16; i++) snprintf(rid + 2 * i, 3, "%02x", c->hdr[0x230 + i]);
        if (!enc) enc = key_get(ks, rid, 16);  // (title.keys: "<rights id> = <title key>")
        if (!enc) {
            say(why, wn, "%s: no title key for it: put title.keys from your Switch with prod.keys (packages "
                "without their ticket need it)", c->name);
            return false;
        }
        snprintf(nm, sizeof nm, "titlekek_%02x", gen);
        const uint8_t *kek = key_get(ks, nm, 16);
        if (!kek) {
            say(why, wn, "your prod.keys has no %s: dump the keys again (Lockpick_RCM) on a Switch with the "
                "latest firmware", nm);
            return false;
        }
        Aes a;
        aes_key(&a, kek);
        aes_dec_c(&a, enc, k);
        memset(&a, 0, sizeof a);
    } else {
        static const char *const kind[3] = {"application", "ocean", "system"};
        int ki = c->hdr[0x207] < 3 ? c->hdr[0x207] : 0;
        snprintf(nm, sizeof nm, "key_area_key_%s_%02x", kind[ki], gen);
        const uint8_t *kak = key_get(ks, nm, 16);
        if (!kak) {
            say(why, wn, "your prod.keys has no %s: dump the keys again (Lockpick_RCM) on a Switch with the "
                "latest firmware", nm);
            return false;
        }
        Aes a;
        aes_key(&a, kak);
        aes_dec_c(&a, c->hdr + 0x300 + 0x20, k);  // (the key area's third key: AES-CTR)
        memset(&a, 0, sizeof a);
    }
    aes_key(&c->key, k);
    memset(k, 0, sizeof k);
    c->keyed = true;
    return true;
}

// ------------------------------------------------------------------ bucket trees (BKTR)
// The entries of a table at [toff, toff + tsize) of the section (decrypted with its own counter), esz bytes each:
// a flat copy, and the end offset of the last range (the node's). hdr: the table's 16-byte header in the FS header
// ("BKTR", version, entry count).
static uint8_t *bktr_load(const Section *s, uint64_t toff, uint64_t tsize, const uint8_t *hdr, size_t esz, int *count,
                          uint64_t *end, char *why, size_t wn) {
    int n = (int)le32(hdr + 8);
    if (memcmp(hdr, "BKTR", 4) || n <= 0 || tsize < BKTR_NODE) {
        say(why, wn, "the update's patch table is unreadable (no BKTR header)");
        return NULL;
    }
    int per = (int)((BKTR_NODE - 16) / esz), sets = (n + per - 1) / per;
    if (sets > (BKTR_NODE - 16) / 8 || (uint64_t)BKTR_NODE * (1 + (uint64_t)sets) > tsize) {
        say(why, wn, "the update's patch table has %d entries: more than this reader handles", n);
        return NULL;
    }
    uint8_t *t = malloc((size_t)tsize), *out = malloc(esz * (size_t)n);
    if (!t || !out || !sect_read(s, t, tsize, toff)) {
        free(t), free(out);
        say(why, wn, "the update's patch table can't be read");
        return NULL;
    }
    int got = 0;
    if ((int)le32(t + 4) != sets) got = -1;
    for (int k = 0; k < sets && got >= 0; k++) {
        const uint8_t *set = t + (size_t)BKTR_NODE * (1 + (size_t)k);
        int cnt = (int)le32(set + 4);
        if ((int)le32(set) != k || cnt <= 0 || cnt > per || got + cnt > n) { got = -1; break; }
        memcpy(out + esz * (size_t)got, set + 16, esz * (size_t)cnt);
        got += cnt;
    }
    *end = le64(t + 8);
    free(t);
    if (got != n) {
        free(out);
        say(why, wn, "the update's patch table doesn't add up (a damaged update package?)");
        return NULL;
    }
    *count = n;
    return out;
}

// ------------------------------------------------------------------ the game
typedef struct {
    int64_t virt, phys;
    int storage;  // (0: the base's section, 1: the update's own bytes)
} Ind;
typedef struct {
    uint64_t off;
    bool clear;     // (stored unencrypted)
    uint32_t gen;   // (the counter's generation for this range)
} Ctx;
typedef struct { const Nca *nca; Section s; uint64_t off, size; uint64_t tid; } Image;  // (a RomFS: IVFC's last level)

struct NspGame {
    Nca *nca;
    int nnca;
    const Nca *base, *upd;
    Section bs, us;  // (the data sections)
    Ind *ind;
    int nind;
    uint64_t ind_end;
    Ctx *ctx;
    int nctx;
    uint64_t ctx_end;
    uint64_t romfs_off, romfs_size;  // (the RomFS in the patched (or base) section)
    Image *aoc;
    int naoc;
    int own[16], nown;  // (nsp_adopt_fds: closed by nsp_close)
    char desc[512];
};

// n bytes at off of the update's section bytes: by AesCtrEx range, each with its own generation
static bool upd_read(const NspGame *g, uint8_t *dst, uint64_t n, uint64_t off) {
    const Section *s = &g->us;
    while (n) {
        uint64_t piece = n;
        uint8_t hi[8];
        memcpy(hi, s->hi, 8);
        bool clear = s->plain;
        if (off < g->ctx_end) {
            int lo = 0, hi_i = g->nctx - 1, at = 0;
            while (lo <= hi_i) {  // (the last range starting at or before off)
                int mid = (lo + hi_i) / 2;
                if (g->ctx[mid].off <= off) at = mid, lo = mid + 1;
                else hi_i = mid - 1;
            }
            uint64_t stop = at + 1 < g->nctx ? g->ctx[at + 1].off : g->ctx_end;
            if (stop - off < piece) piece = stop - off;
            uint32_t gen = g->ctx[at].gen;
            hi[4] = (uint8_t)(gen >> 24), hi[5] = (uint8_t)(gen >> 16), hi[6] = (uint8_t)(gen >> 8), hi[7] = (uint8_t)gen;
            clear = clear || g->ctx[at].clear;
        }
        if (s->start + off + piece > s->end || !ctr_read(s->nca, hi, clear, dst, piece, s->start + off)) return false;
        dst += piece, off += piece, n -= piece;
    }
    return true;
}

// n bytes at off of the patched section: by indirect range, from the base's section or the update's bytes
static bool patched_read(const NspGame *g, uint8_t *dst, uint64_t n, uint64_t off) {
    while (n) {
        if (off >= g->ind_end) return false;
        int lo = 0, hi = g->nind - 1, at = 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if ((uint64_t)g->ind[mid].virt <= off) at = mid, lo = mid + 1;
            else hi = mid - 1;
        }
        const Ind *e = &g->ind[at];
        uint64_t stop = at + 1 < g->nind ? (uint64_t)g->ind[at + 1].virt : g->ind_end;
        uint64_t piece = stop - off < n ? stop - off : n, p = (uint64_t)e->phys + (off - (uint64_t)e->virt);
        bool ok = e->storage == 0 ? sect_read(&g->bs, dst, piece, p) : upd_read(g, dst, piece, p);
        if (!ok) return false;
        dst += piece, off += piece, n -= piece;
    }
    return true;
}

// the RomFS level of a section's IVFC tree (hash type 3): its offset and size in the section
static bool ivfc_romfs(const uint8_t *fsh, uint64_t *off, uint64_t *size) {
    const uint8_t *hd = fsh + 8;
    if (fsh[3] != 3 || memcmp(hd, "IVFC", 4)) return false;
    uint32_t layers = le32(hd + 0x0C);
    if (layers < 2 || layers > 7) return false;
    const uint8_t *lv = hd + 0x10 + 0x18 * (layers - 2);
    *off = le64(lv), *size = le64(lv + 8);
    return *size > 0;
}

// a section kind this reader doesn't do: sparse or compressed storage (newer packages; not Z-A's so far)
static bool odd_storage(const uint8_t *fsh) {
    return le32(fsh + 0x148 + 0x10 + 8) != 0 || le32(fsh + 0x178 + 0x10 + 8) != 0;
}

static bool data_section(const Nca *c, Section *s) {
    return section(c, c->hdr[0x205] == 0 ? 1 : 0, s);  // (a program's data is section 1, a data NCA's section 0)
}

NspGame *nsp_open(const int *fds, const char *const *names, int n, const char *prod_keys, const char *title_keys,
                  char *why, size_t wn) {
    if (!aes_init(why, wn)) return NULL;
    Keys ks = {0};
    keys_parse(&ks, prod_keys);
    keys_parse(&ks, title_keys);
    NspGame *g = calloc(1, sizeof *g);
    TitleKey *tk = NULL;
    int ntk = 0;
    const uint8_t *hk = key_get(&ks, "header_key", 32);
    if (!g) goto fail;
    if (!hk) {
        say(why, wn, "prod.keys has no header_key: it isn't a complete prod.keys. Dump the keys from your Switch "
            "with Lockpick_RCM and use its prod.keys");
        goto fail;
    }
    for (int p = 0; p < n; p++) {
        PfsFile *f;
        const char *pn = names && names[p] ? names[p] : "a package";
        int nf = pfs0_list(plain_read, &fds[p], 0, &f);
        if (nf < 0) {
            uint8_t m[4] = {0};
            bool xci = rd(fds[p], m, 4, 0x100) && !memcmp(m, "HEAD", 4);
            say(why, wn, xci ? "%s: an .xci - choose the game as .nsp (this reader takes .nsp packages)"
                             : "%s: not an .nsp (or not completely copied): dump it again from your Switch", pn);
            goto fail;
        }
        for (int i = 0; i < nf; i++) {
            if (ends_with(f[i].name, ".tik") && f[i].size >= 0x2C0 && f[i].size <= 0x1000) {
                uint8_t t[0x400] = {0};
                if (!rd(fds[p], t, f[i].size < sizeof t ? f[i].size : sizeof t, f[i].off)) continue;
                uint32_t st = le32(t);  // (the signature type sets where the ticket's data starts)
                size_t d = st == 0x10000 || st == 0x10003 ? 4 + 0x200 + 0x3C
                         : st == 0x10001 || st == 0x10004 ? 4 + 0x100 + 0x3C
                         : st == 0x10002 || st == 0x10005 ? 4 + 0x3C + 0x40 : 0;
                if (!d || d + 0x170 > sizeof t || t[d + 0x141] != 0) continue;  // (0: a common ticket; personalized
                                                                                 // ones need the console's own key)
                TitleKey *grown = realloc(tk, sizeof *tk * (size_t)(ntk + 1));
                if (!grown) continue;
                tk = grown;
                memcpy(tk[ntk].rights, t + d + 0x160, 16), memcpy(tk[ntk].key, t + d + 0x40, 16);
                ntk++;
            } else if (ends_with(f[i].name, ".nca") && f[i].size >= NCA_HDR) {
                Nca *grown = realloc(g->nca, sizeof *g->nca * (size_t)(g->nnca + 1));
                if (!grown) continue;
                g->nca = grown;
                Nca *c = &g->nca[g->nnca];
                memset(c, 0, sizeof *c);
                c->fd = fds[p], c->base = f[i].off, c->size = f[i].size;
                snprintf(c->name, sizeof c->name, "%s/%s", pn, f[i].name);
                if (!rd(c->fd, c->hdr, NCA_HDR, c->base)) continue;
                xts_header(hk, c->hdr);
                if (memcmp(c->hdr + 0x200, "NCA3", 4)) continue;  // (not this kind, or not decryptable: skipped)
                g->nnca++;
            }
        }
        free(f);
    }
    // Z-A's program NCAs (the update's program carries the base title id too, its data section a patch) and DLC
    for (int i = 0; i < g->nnca; i++) {
        Nca *c = &g->nca[i];
        uint64_t tid = le64(c->hdr + 0x210);
        Section s;
        if ((tid & ~0x1FFFull) != ZA_BASE || !data_section(c, &s)) continue;
        bool patch = le64(s.fsh + 0x108) != 0;  // (an indirect table: LibHac's IsPatchSection)
        if (c->hdr[0x205] == 0) {
            if (patch) g->upd = c;
            else if (tid == ZA_BASE) g->base = c;
        } else if (c->hdr[0x205] == 5) {
            Image *grown = realloc(g->aoc, sizeof *g->aoc * (size_t)(g->naoc + 1));
            if (!grown) continue;
            g->aoc = grown;
            Image *im = &g->aoc[g->naoc];
            im->nca = c, im->s = s, im->tid = tid;
            if (ivfc_romfs(s.fsh, &im->off, &im->size) && im->off + im->size <= s.end - s.start) g->naoc++;
        }
    }
    if (!g->base) {
        say(why, wn, g->nnca ? "the base game of Pokémon Legends Z-A isn't among these files"
                             : "no game packages found (or prod.keys isn't this Switch's: its header_key opens none)");
        goto fail;
    }
    if (!nca_keyed((Nca *)g->base, &ks, tk, ntk, why, wn) || (g->upd && !nca_keyed((Nca *)g->upd, &ks, tk, ntk, why, wn)))
        goto fail;
    // a DLC without its key is left out, not the game (it runs without it: zaimport needed EXCLUDE= for a DLC dumped
    // without its ticket - tools/linux/android-port/import_za.sh)
    int skipped = 0;
    char skip_why[256] = "";
    for (int k = 0; k < g->naoc;) {
        char dw[256];
        if (nca_keyed((Nca *)g->aoc[k].nca, &ks, tk, ntk, dw, sizeof dw)) k++;
        else {
            if (!skipped++) snprintf(skip_why, sizeof skip_why, "%s", dw);
            memmove(&g->aoc[k], &g->aoc[k + 1], sizeof *g->aoc * (size_t)(g->naoc - k - 1)), g->naoc--;
        }
    }
    if (!data_section(g->base, &g->bs) || odd_storage(g->bs.fsh)) {
        say(why, wn, "%s: its game data is in a form this reader doesn't handle", g->base->name);
        goto fail;
    }
    if (!g->upd) {
        if (!ivfc_romfs(g->bs.fsh, &g->romfs_off, &g->romfs_size) || g->romfs_off + g->romfs_size > g->bs.end - g->bs.start) {
            say(why, wn, "%s: no RomFS in its data section", g->base->name);
            goto fail;
        }
    } else {
        Section *u = &g->us;
        if (!data_section(g->upd, u) || odd_storage(u->fsh) || (u->fsh[4] != 4 && u->fsh[4] != 6)) {
            say(why, wn, "%s: its patch is in a form this reader doesn't handle", g->upd->name);
            goto fail;
        }
        const uint8_t *pi = u->fsh + 0x100;
        uint8_t *raw = bktr_load(u, le64(pi), le64(pi + 8), pi + 0x10, 0x14, &g->nind, &g->ind_end, why, wn);
        if (!raw) goto fail;
        g->ind = malloc(sizeof *g->ind * (size_t)g->nind);
        for (int i = 0; g->ind && i < g->nind; i++) {
            const uint8_t *e = raw + 0x14 * i;
            g->ind[i].virt = (int64_t)le64(e), g->ind[i].phys = (int64_t)le64(e + 8), g->ind[i].storage = (int)le32(e + 16);
        }
        free(raw);
        raw = bktr_load(u, le64(pi + 0x20), le64(pi + 0x28), pi + 0x30, 0x10, &g->nctx, &g->ctx_end, why, wn);
        if (!raw || !g->ind) { free(raw); goto fail; }
        g->ctx = malloc(sizeof *g->ctx * (size_t)g->nctx);
        for (int i = 0; g->ctx && i < g->nctx; i++) {
            const uint8_t *e = raw + 0x10 * i;
            g->ctx[i].off = le64(e), g->ctx[i].clear = e[8] == 1, g->ctx[i].gen = le32(e + 12);
        }
        free(raw);
        if (!g->ctx) goto fail;
        for (int i = 0; i < g->nind; i++)
            if (g->ind[i].storage > 1 || (i && g->ind[i].virt <= g->ind[i - 1].virt)) {
                say(why, wn, "the update's patch table is damaged: copy or dump the update again");
                goto fail;
            }
        if (!ivfc_romfs(u->fsh, &g->romfs_off, &g->romfs_size) || g->romfs_off + g->romfs_size > g->ind_end) {
            say(why, wn, "%s: no RomFS in its patch", g->upd->name);
            goto fail;
        }
    }
    snprintf(g->desc, sizeof g->desc, "%s, %d DLC%s%s%s, AES %s", g->upd ? "base game + update" : "base game", g->naoc,
             skipped ? " (+ DLC left out: " : "", skip_why, skipped ? ")" : "", g_ctr_name);
    free(tk);
    keys_free(&ks);
    return g;
fail:
    if (tk) memset(tk, 0, sizeof *tk * (size_t)ntk);
    free(tk);
    keys_free(&ks);
    nsp_close(g);
    return NULL;
}

// ---- one package on its own (nsp.h nsp_classify / nsp_report): what it holds, for the start screen's steps
static int kind_of(uint64_t tid, int ctype, bool patch) {
    if ((tid & ~0x1FFFull) != ZA_BASE) return NSP_OTHER_GAME;
    if (ctype == 0) return patch ? NSP_UPDATE : tid == ZA_BASE ? NSP_BASE : NSP_OTHER_GAME;
    return ctype == 5 ? NSP_DLC : NSP_UNKNOWN;
}
// (from a ticket's name, "<rights id: title id + 0 + key generation>.tik", when there are no keys to open the NCAs)
static int kind_of_tid(uint64_t tid) {
    if ((tid & ~0x1FFFull) != ZA_BASE) return NSP_OTHER_GAME;
    return tid == ZA_BASE ? NSP_BASE : tid == ZA_BASE + 0x800 ? NSP_UPDATE : NSP_DLC;
}

int nsp_classify(int fd, const char *prod_keys, uint64_t *tid_out, char *report, size_t rn) {
    uint64_t tid_seen = 0;
    if (report && rn) report[0] = 0;
    size_t rl = 0;
#define REP(...) do { if (report && rl < rn) rl += (size_t)snprintf(report + rl, rn - rl, __VA_ARGS__); } while (0)
    PfsFile *f;
    int nf = pfs0_list(plain_read, &fd, 0, &f);
    if (nf < 0) {
        uint8_t m[4] = {0};
        return rd(fd, m, 4, 0x100) && !memcmp(m, "HEAD", 4) ? NSP_XCI : NSP_NOT_PACKAGE;
    }
    int kind = NSP_UNKNOWN, rank = 0;  // (a program NCA says more than a data one, either more than a ticket)
    Keys ks = {0};
    keys_parse(&ks, prod_keys);
    const uint8_t *hk = key_get(&ks, "header_key", 32);
    if (hk && aes_init(NULL, 0)) {
        for (int i = 0; i < nf; i++) {
            if (!ends_with(f[i].name, ".nca") || f[i].size < NCA_HDR) continue;
            Nca c;
            memset(&c, 0, sizeof c);
            c.fd = fd, c.base = f[i].off, c.size = f[i].size;
            if (!rd(fd, c.hdr, NCA_HDR, c.base)) continue;
            xts_header(hk, c.hdr);
            if (memcmp(c.hdr + 0x200, "NCA3", 4)) { REP("%s: not an NCA3 (or not this prod.keys)\n", f[i].name); continue; }
            uint64_t tid = le64(c.hdr + 0x210);
            int ct = c.hdr[0x205];
            Section s;
            bool patch = ct == 0 && data_section(&c, &s) && le64(s.fsh + 0x108) != 0;
            char rid[33] = "-";
            if (rights_id(&c)) for (int k = 0; k < 16; k++) snprintf(rid + 2 * k, 3, "%02x", c.hdr[0x230 + k]);
            REP("%s: type %d, title %016llx%s, rights id %s\n", f[i].name, ct, (unsigned long long)tid,
                patch ? " (patch)" : "", rid);
            int r = ct == 0 ? 3 : ct == 5 ? 2 : 0;
            if (r > rank) rank = r, kind = kind_of(tid, ct, patch), tid_seen = tid;
        }
    }
    for (int i = 0; i < nf; i++)
        if (ends_with(f[i].name, ".tik")) {
            REP("%s: ticket\n", f[i].name);
            char hex[17];
            if (rank < 1 && strlen(f[i].name) >= 36 && strspn(f[i].name, "0123456789abcdefABCDEF") >= 32) {
                memcpy(hex, f[i].name, 16), hex[16] = 0;
                tid_seen = strtoull(hex, NULL, 16), kind = kind_of_tid(tid_seen), rank = 1;
            }
        }
#undef REP
    keys_free(&ks);
    free(f);
    if (tid_out) *tid_out = tid_seen;
    return kind;
}

void nsp_close(NspGame *g) {
    if (!g) return;
    if (g->nca) memset(g->nca, 0, sizeof *g->nca * (size_t)g->nnca);  // (the section keys)
    free(g->nca), free(g->ind), free(g->ctx), free(g->aoc);
    for (int i = 0; i < g->nown; i++) close(g->own[i]);
    free(g);
}

void nsp_adopt_fds(NspGame *g, const int *fds, int n) {
    for (int i = 0; i < n && g->nown < 16; i++) g->own[g->nown++] = fds[i];
}

const char *nsp_describe(const NspGame *g) { return g->desc; }
uint64_t nsp_romfs_size(const NspGame *g) { return g->romfs_size; }

ssize_t nsp_romfs_read(const NspGame *g, void *dst, uint64_t n, uint64_t off) {
    if (off >= g->romfs_size) return 0;
    if (n > g->romfs_size - off) n = g->romfs_size - off;
    bool ok = g->upd ? patched_read(g, dst, n, g->romfs_off + off) : sect_read(&g->bs, dst, n, g->romfs_off + off);
    return ok ? (ssize_t)n : -1;
}

int nsp_aoc_count(const NspGame *g) { return g->naoc; }
uint64_t nsp_aoc_title(const NspGame *g, int i) { return g->aoc[i].tid; }
uint64_t nsp_aoc_size(const NspGame *g, int i) { return g->aoc[i].size; }
ssize_t nsp_aoc_read(const NspGame *g, int i, void *dst, uint64_t n, uint64_t off) {
    const Image *im = &g->aoc[i];
    if (off >= im->size) return 0;
    if (n > im->size - off) n = im->size - off;
    return sect_read(&im->s, dst, n, im->off + off) ? (ssize_t)n : -1;
}

bool nsp_exefs_extract(const NspGame *g, const char *dir, char *why, size_t wn) {
    const Nca *c = g->upd ? g->upd : g->base;
    Section s;
    const uint8_t *hd;
    if (!section(c, 0, &s) || s.fsh[3] != 2 || (hd = s.fsh + 8, le32(hd + 0x24) < 1 || le32(hd + 0x24) > 5)) {
        say(why, wn, "%s: no code section", c->name);
        return false;
    }
    const uint8_t *region = hd + 0x28 + 0x10 * (le32(hd + 0x24) - 1);  // (the hash tree's last region: the PFS0)
    PfsFile *f;
    int nf = pfs0_list(sect_read, &s, le64(region), &f);
    if (nf < 0) {
        say(why, wn, "%s: its code section can't be read (is prod.keys from your Switch?)", c->name);
        return false;
    }
    bool ok = true;
    for (int i = 0; i < nf && ok; i++) {
        const char *nm = f[i].name;
        if (!*nm || strchr(nm, '/') || strchr(nm, '\\') || !strcmp(nm, "..") || !strcmp(nm, ".")) continue;
        char path[1200], part[1210];
        snprintf(path, sizeof path, "%s/%s", dir, nm);
        snprintf(part, sizeof part, "%s.part", path);
        uint8_t *b = f[i].size <= (256u << 20) ? malloc(f[i].size ? (size_t)f[i].size : 1) : NULL;
        FILE *o = b ? fopen(part, "wb") : NULL;
        ok = o && sect_read(&s, b, f[i].size, f[i].off) && fwrite(b, 1, (size_t)f[i].size, o) == f[i].size;
        if (o && fclose(o)) ok = false;
        free(b);
        if (ok) {
            remove(path);
            ok = rename(part, path) == 0;
        } else {
            remove(part);
            say(why, wn, "exefs/%s couldn't be written (%s)", nm, strerror(errno));
        }
    }
    free(f);
    return ok;
}
