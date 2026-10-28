/* Random well-formed LZ4 blocks against ../lz4.c, exact-size heap buffers,
   built with ASan/UBSan. The generator builds the raw data alongside the
   stream, so the expected output does not come from any decoder. Every
   block obeys the end-of-block rules (last 5 bytes literals, last match
   starting at least 12 bytes before the end; blocks under 13 bytes are all
   literals). Usage: lz4fuzz SEED COUNT [DUMPDIR]; DUMPDIR gets every 997th
   block as raw/comp pairs for a cross-check with the reference decoder. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lz4.h"

static uint64_t s_rng;
static uint32_t rnd(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return (uint32_t)(s_rng >> 16);
}
static uint32_t pick(uint32_t n) { return n ? rnd() % n : 0; }

/* Length biased toward the encoding edges. */
static uint32_t len(uint32_t base) {
    static const uint32_t edges[] = {0, 1, 2, 3, 13, 14, 15, 16, 17, 268, 269, 270, 271, 523, 524, 525, 526, 1000};
    switch (pick(4)) {
        case 0: {
            uint32_t e = edges[pick(sizeof(edges) / sizeof(edges[0]))];
            return e >= base ? e - base : 0;
        }
        case 1:
            return pick(8);
        case 2:
            return pick(64);
        default:
            return pick(2000);
    }
}

static uint8_t *s_raw, *s_comp;
static size_t s_rawlen, s_complen;

static void emitLen(size_t n) {
    while (n >= 255) {
        s_comp[s_complen++] = 255;
        n -= 255;
    }
    s_comp[s_complen++] = n;
}

static void gen(void) {
    s_rawlen = s_complen = 0;
    uint32_t nseq = pick(4) == 0 ? 0 : pick(40);
    for (uint32_t q = 0; q < nseq; q++) {
        size_t lit = len(0);
        if (s_rawlen + lit == 0) lit = 1 + pick(20);
        size_t ml = 4 + len(4);
        size_t token = s_complen++;
        size_t ln = lit;
        if (ln >= 15) emitLen(ln - 15);
        int fill = pick(3);
        for (size_t i = 0; i < lit; i++) {
            uint8_t b = fill == 0 ? rnd() : fill == 1 ? 'a' + pick(4) : 0;
            s_comp[s_complen++] = b;
            s_raw[s_rawlen++] = b;
        }
        size_t maxoff = s_rawlen > 65535 ? 65535 : s_rawlen;
        size_t off;
        switch (pick(4)) {
            case 0:
                off = 1 + pick(maxoff < 8 ? maxoff : 8);
                break;
            case 1:
                off = maxoff;
                break;
            default:
                off = 1 + pick(maxoff);
        }
        s_comp[s_complen++] = off & 0xff;
        s_comp[s_complen++] = off >> 8;
        size_t mn = ml - 4;
        if (mn >= 15) emitLen(mn - 15);
        s_comp[token] = (uint8_t)(((ln < 15 ? ln : 15) << 4) | (mn < 15 ? mn : 15));
        for (size_t i = 0; i < ml; i++, s_rawlen++) s_raw[s_rawlen] = s_raw[s_rawlen - off];
    }
    /* A trailer of >= 12 literals after any match keeps the last match
       starting at least 12 bytes before the end. */
    size_t tail = 5 + len(5);
    if (nseq && tail < 12) tail = 12;
    size_t token = s_complen++;
    if (tail >= 15) emitLen(tail - 15);
    s_comp[token] = (uint8_t)((tail < 15 ? tail : 15) << 4);
    for (size_t i = 0; i < tail; i++) {
        uint8_t b = rnd();
        s_comp[s_complen++] = b;
        s_raw[s_rawlen++] = b;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    s_rng = strtoull(argv[1], 0, 0) * 0x9e3779b97f4a7c15ull + 1;
    unsigned long count = strtoul(argv[2], 0, 0);
    const char *dump = argc > 3 ? argv[3] : 0;
    s_raw = malloc(4 << 20);
    s_comp = malloc(4 << 20);
    unsigned long dumped = 0;
    for (unsigned long n = 0; n < count; n++) {
        gen();
        uint8_t *in = malloc(s_complen);
        memcpy(in, s_comp, s_complen);
        uint8_t *out = malloc(s_rawlen ? s_rawlen : 1);
        lz4_decompress_block(in, in + s_complen, out);
        if (memcmp(out, s_raw, s_rawlen)) {
            printf("MISMATCH at block %lu rawlen %zu complen %zu\n", n, s_rawlen, s_complen);
            FILE *f = fopen("fuzz-fail.raw", "wb");
            fwrite(s_raw, 1, s_rawlen, f);
            fclose(f);
            f = fopen("fuzz-fail.lz4", "wb");
            fwrite(s_comp, 1, s_complen, f);
            fclose(f);
            return 1;
        }
        if (dump && n % 997 == 0) {
            char p[512];
            snprintf(p, sizeof(p), "%s/%06lu.raw", dump, dumped);
            FILE *f = fopen(p, "wb");
            fwrite(s_raw, 1, s_rawlen, f);
            fclose(f);
            snprintf(p, sizeof(p), "%s/%06lu.lz4", dump, dumped++);
            f = fopen(p, "wb");
            fwrite(s_comp, 1, s_complen, f);
            fclose(f);
        }
        free(in);
        free(out);
    }
    printf("OK %lu blocks\n", count);
    return 0;
}
