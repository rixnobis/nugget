/* The random corpus (lz4gen.h) through the candidate under qemu-mipsel:
   seeds 1..5 with the host's block counts, one alignment pair per block,
   guards on both sides, pre-filled output. Prints each seed's digest (must
   match lz4fuzz) and the failure count; exit code = failures. */
#include <stdint.h>

void lz4_cand1(const void *, const void *, void *);
void qwrite(const char *, uint32_t);
static uint8_t s_rawbuf[192 * 1024], s_compbuf[192 * 1024];
#define GEN_RAW s_rawbuf
#define GEN_COMP s_compbuf
#include "../lz4gen.h"
static uint8_t src[192 * 1024 + 4], out[256 + 192 * 1024 + 4 + 256];

static void puts_(const char *s) { uint32_t n = 0; while (s[n]) n++; qwrite(s, n); }
static void puth(uint32_t v) { char b[9]; for (int i = 7; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15]; b[8] = 0; puts_(b); }
static void putn(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); puts_(b + i); }

int qmain(void) {
    static const uint32_t counts[6] = {0, 20000, 50000, 50000, 50000, 50000};
    uint32_t fails = 0, total = 0;
    for (int seed = 1; seed <= 5; seed++) {
        genSeed(seed);
        uint32_t dg = 2166136261u;
        for (uint32_t n = 0; n < counts[seed]; n++) {
            gen();
            dg = genDigest(dg);
            uint32_t so = (n >> 2) & 3, d = n & 3;
            uint8_t f = (n & 1) ? 0x5a : 0xa5;
            for (uint32_t i = 0; i < s_complen; i++) src[so + i] = s_compbuf[i];
            for (uint32_t i = 0; i < 256 + d + s_rawlen + 256; i++) out[i] = f;
            lz4_cand1(src + so, src + so + s_complen, out + 256 + d);
            int bad = 0;
            for (uint32_t i = 0; i < s_rawlen; i++) if (out[256 + d + i] != s_rawbuf[i]) { bad = 1; break; }
            for (uint32_t i = 0; i < 256 + d; i++) if (out[i] != f) bad = 1;
            for (uint32_t i = 0; i < 256; i++) if (out[256 + d + s_rawlen + i] != f) bad = 1;
            if (bad) { if (fails < 10) { puts_("FAIL seed "); putn(seed); puts_(" block "); putn(n); puts_("\n"); } fails++; }
            total++;
        }
        puts_("qrand: seed "); putn(seed); puts_(" blocks "); putn(counts[seed]); puts_(" digest "); puth(dg); puts_("\n");
    }
    puts_("qrand: blocks "); putn(total); puts_(" failures "); putn(fails); puts_("\n");
    return fails;
}
