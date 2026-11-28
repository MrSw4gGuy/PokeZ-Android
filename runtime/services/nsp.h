// Pokémon Legends Z-A's data read straight from the player's own .nsp packages (base game + update + DLC) with their
// prod.keys, decrypted as it is read: no romfs.bin beside them, so the game isn't kept twice on a phone, and the same
// packages stay usable by an emulator (Eden...). Only the small exefs/ is written out (the game code is built from it).
// The container reading (PFS0, NCA headers, tickets, the update's BKTR patch) is done here, in plain C with no other
// library: the NCA header in AES-128-XTS, the data in AES-128-CTR (ARMv8 / AES-NI instructions when the CPU has them).
// Self-contained (no runtime headers): tools/nsp_check.c tests it against a romfs.bin made by tools/zaimport.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct NspGame NspGame;

// The game from these packages (fds open for reading, any order; names only for messages): Z-A's base program NCA,
// its update's when there is one (the RomFS then reads through the update's patch), and its DLC data. prod_keys /
// title_keys: the keys files' text (title_keys may be NULL: only for packages without their ticket). NULL and why
// filled when it can't be read. The fds stay the caller's: kept open while the NspGame is used, closed after nsp_close.
NspGame *nsp_open(const int *fds, const char *const *names, int n, const char *prod_keys, const char *title_keys,
                  char *why, size_t wn);
void nsp_close(NspGame *g);
// the fds become the NspGame's: nsp_close closes them (up to 16)
void nsp_adopt_fds(NspGame *g, const int *fds, int n);

// "base + update" / "base" and the DLC count, for the log
const char *nsp_describe(const NspGame *g);

// The RomFS image the game reads (the same bytes as zaimport's romfs.bin): size, and n bytes at off (past the end:
// short). Thread-safe (nothing in the NspGame changes after nsp_open). -1 when the package can't be read.
uint64_t nsp_romfs_size(const NspGame *g);
ssize_t nsp_romfs_read(const NspGame *g, void *dst, uint64_t n, uint64_t off);

// The program's exefs (the update's when there is one: main, main.npdm, rtld, sdk...) written into dir, which must
// exist. false and why filled on a failure.
bool nsp_exefs_extract(const NspGame *g, const char *dir, char *why, size_t wn);

// The DLC RomFS images (zaimport's aoc/<title id>.bin): how many, title id, size, and a read like nsp_romfs_read
int nsp_aoc_count(const NspGame *g);
uint64_t nsp_aoc_title(const NspGame *g, int i);
uint64_t nsp_aoc_size(const NspGame *g, int i);
ssize_t nsp_aoc_read(const NspGame *g, int i, void *dst, uint64_t n, uint64_t off);

// Which AES the CTR path uses ("ARMv8", "AES-NI" or "C"), for the log
const char *nsp_aes_impl(void);

// ---- the game folder's packages (runtime/services/nsp_list.c, the runtime's side: not in tools/nsp_check)
// <game dir>/packages.txt lists them, one a line: a path, or on Android the content:// URI the system file picker gave
// (the packages stay where the player keeps them - Eden's games folder, Downloads - readable by an emulator too), and
// the keys the setup copied beside it: keys/prod.keys (+ keys/title.keys). The game reads its RomFS through this
// (runtime/services/fs.c) when packages.txt is there.
#define NSP_LIST "packages.txt"
NspGame *nsp_open_game_dir(const char *game_dir, char *why, size_t wn);
// a package source opened for reading: a path, or (Android) a content:// URI whose read permission the app keeps
// (keep: ask Android to hold it across restarts - right after the picker gave it). -1 if it can't be opened.
int nsp_open_source(const char *src, bool keep);
// a small text file (keys) as a string, NULL if missing or over max bytes; free() it
char *nsp_slurp_fd(int fd, size_t max);
// a source's file name for messages (a URI's last part, decoded; "a package" when it carries none)
void nsp_source_name(const char *src, char *out, size_t n);
