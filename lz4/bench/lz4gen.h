/* Random well-formed LZ4 block generator, shared by the host fuzzer
   (lz4fuzz.c), the qemu pre-check and the PS1 random-corpus run (rand/), so
   all three see the same blocks for a given seed. The raw data is built
   alongside the stream; the expected output never comes from a decoder.
   Every block obeys the end-of-block rules: last 5 bytes are literals, the
   last match starts at least 12 bytes before the end, blocks with no match
   are all literals. 32-bit arithmetic only, so the PS1 build needs no libgcc.
   Include once, after defining GEN_RAW and GEN_COMP (byte arrays of at least
   GEN_MAX bytes). */

#include <stdint.h>

#define GEN_MAX (192 * 1024)

/* Initial xorshift states, seed * 0x9e3779b97f4a7c15 + 1 (lz4fuzz.c
   checks this table against the formula). Seeds 1..5. */
static const uint32_t s_genSeeds[6][2] = {
    {0x00000001u, 0x00000000u}, {0x7f4a7c16u, 0x9e3779b9u}, {0xfe94f82bu, 0x3c6ef372u},
    {0x7ddf7440u, 0xdaa66d2cu}, {0xfd29f055u, 0x78dde6e5u}, {0x7c746c6au, 0x1715609fu},
};

static uint32_t s_genLo, s_genHi;
static uint32_t s_rawlen, s_complen;

static void genSeed(int seed) {
    s_genLo = s_genSeeds[seed][0];
    s_genHi = s_genSeeds[seed][1];
}

/* 64-bit xorshift (13, 7, 17) on a hi:lo pair; returns bits 16..47. */
static uint32_t rnd(void) {
    uint32_t lo = s_genLo, hi = s_genHi;
    hi ^= (hi << 13) | (lo >> 19);
    lo ^= lo << 13;
    lo ^= (lo >> 7) | (hi << 25);
    hi ^= hi >> 7;
    hi ^= (hi << 17) | (lo >> 15);
    lo ^= lo << 17;
    s_genLo = lo;
    s_genHi = hi;
    return (lo >> 16) | (hi << 16);
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

static void emitLen(uint32_t n) {
    while (n >= 255) {
        GEN_COMP[s_complen++] = 255;
        n -= 255;
    }
    GEN_COMP[s_complen++] = n;
}

static void gen(void) {
    s_rawlen = s_complen = 0;
    uint32_t nseq = pick(4) == 0 ? 0 : pick(40);
    for (uint32_t q = 0; q < nseq; q++) {
        uint32_t lit = len(0);
        if (s_rawlen + lit == 0) lit = 1 + pick(20);
        uint32_t ml = 4 + len(4);
        uint32_t token = s_complen++;
        if (lit >= 15) emitLen(lit - 15);
        int fill = pick(3);
        for (uint32_t i = 0; i < lit; i++) {
            uint8_t b = fill == 0 ? rnd() : fill == 1 ? 'a' + pick(4) : 0;
            GEN_COMP[s_complen++] = b;
            GEN_RAW[s_rawlen++] = b;
        }
        uint32_t maxoff = s_rawlen > 65535 ? 65535 : s_rawlen;
        uint32_t off;
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
        GEN_COMP[s_complen++] = off & 0xff;
        GEN_COMP[s_complen++] = off >> 8;
        uint32_t mn = ml - 4;
        if (mn >= 15) emitLen(mn - 15);
        GEN_COMP[token] = (uint8_t)(((lit < 15 ? lit : 15) << 4) | (mn < 15 ? mn : 15));
        for (uint32_t i = 0; i < ml; i++, s_rawlen++) GEN_RAW[s_rawlen] = GEN_RAW[s_rawlen - off];
    }
    /* A trailer of >= 12 literals after any match keeps the last match
       starting at least 12 bytes before the end. */
    uint32_t tail = 5 + len(5);
    if (nseq && tail < 12) tail = 12;
    uint32_t token = s_complen++;
    if (tail >= 15) emitLen(tail - 15);
    GEN_COMP[token] = (uint8_t)((tail < 15 ? tail : 15) << 4);
    for (uint32_t i = 0; i < tail; i++) {
        uint8_t b = rnd();
        GEN_COMP[s_complen++] = b;
        GEN_RAW[s_rawlen++] = b;
    }
}

/* FNV-1a over a block's lengths and bytes, chained across blocks: equal
   digests on two machines mean the same corpus was generated on both. */
static uint32_t genDigest(uint32_t h) {
    uint32_t w[2] = {s_rawlen, s_complen};
    const uint8_t *p = (const uint8_t *)w;
    for (int i = 0; i < 8; i++) h = (h ^ p[i]) * 16777619u;
    for (uint32_t i = 0; i < s_rawlen; i++) h = (h ^ GEN_RAW[i]) * 16777619u;
    for (uint32_t i = 0; i < s_complen; i++) h = (h ^ GEN_COMP[i]) * 16777619u;
    return h;
}
