/* lz4 duel bench. Every implementation in s_impls runs the same gate and the
   same timing loop:
   - normal: each corpus stream, source and destination at the 16 alignment
     combinations, 256-byte guards on both sides of the output, output compared
     byte for byte with the raw data;
   - top: compressed input ending against 0x80200000 behind a guard, output
     ending against a guard just below it;
   - inplace: compressed data at the tail of a raw + margin buffer, margin = the
     baseline's measured bar from mkcorpus.py; the baseline also runs margin - 1
     as the arm that must be able to fail;
   - timing: timed payloads only, cold i-cache (4 KB sled before each call),
     interrupts off, RUNS runs each, cycles = counter 2 delta disambiguated by
     the hblank counter.
   Output lines start with "lz4duel:"; exit code 0 when every gate passed. */

#include <stdint.h>

#include "common/hardware/counters.h"
#include "common/syscalls/syscalls.h"
#include "lz4/lz4.h"

static void *memcpy(void *d, const void *s, uint32_t n) {
    uint8_t *dp = d;
    const uint8_t *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

static void *memset(void *d, int c, uint32_t n) {
    uint8_t *dp = d;
    while (n--) *dp++ = c;
    return d;
}

static int memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *ap = a, *bp = b;
    for (; n; n--, ap++, bp++)
        if (*ap != *bp) return *ap < *bp ? -1 : 1;
    return 0;
}

typedef void (*DecompressFn)(const void *, const void *, void *);

struct Impl {
    const char *name;
    DecompressFn fn;
    int baseline;
};

static const struct Impl s_impls[] = {
    {"baseline", lz4_decompress_block, 1},
#ifdef BENCH_CANDIDATES
    BENCH_CANDIDATES
#endif
};
#define NIMPLS (sizeof(s_impls) / sizeof(s_impls[0]))

#define RUNS 9
#define GUARD 256
#define GUARDBYTE 0xa5
#define RAMTOP 0x80200000u

extern const uint8_t g_corpus[];
extern uint8_t __bss_end[];
void benchSled(void);
void benchOnStack(void (*fn)(void), void *sp);
uint32_t benchTime(DecompressFn fn, const void *src, const void *end, void *dst, uint32_t *hblanks);

struct Stream {
    char name[24];
    uint32_t flags, rawlen, complen, margin;
    const uint8_t *raw, *comp;
};

static uint8_t s_work[GUARD + 70 * 1024 + 4 + GUARD];
static uint8_t s_src[70 * 1024 + 4];
static uint32_t s_stack[4096];
static uint8_t s_save[140 * 1024];
static int s_exit;

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static const uint8_t *parse(const uint8_t *p, struct Stream *s) {
    memcpy(s->name, p, 24);
    s->name[23] = 0;
    s->flags = rd32(p + 24);
    s->rawlen = rd32(p + 28);
    s->complen = rd32(p + 32);
    s->margin = rd32(p + 36);
    p += 40;
    s->raw = p;
    p += (s->rawlen + 3) & ~3;
    s->comp = p;
    p += (s->complen + 3) & ~3;
    return p;
}

static int guardOk(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (p[i] != GUARDBYTE) return 0;
    return 1;
}

static void enter(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12" : : "r"(sr & ~1u));
}

static void leave(void) {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    __asm__ volatile("mtc0 %0, $12" : : "r"(sr | 1u));
}

/* Normal arm: returns the number of failing alignment combinations. */
static int gateNormal(const struct Impl *im, const struct Stream *s) {
    int bad = 0;
    for (int so = 0; so < 4; so++) {
        memcpy(s_src + so, s->comp, s->complen);
        for (int d = 0; d < 4; d++) {
            uint8_t *dst = s_work + GUARD + d;
            memset(s_work, GUARDBYTE, sizeof(s_work));
            enter();
            im->fn(s_src + so, s_src + so + s->complen, dst);
            leave();
            if (memcmp(dst, s->raw, s->rawlen) || !guardOk(s_work, GUARD + d) ||
                !guardOk(dst + s->rawlen, GUARD))
                bad++;
        }
    }
    return bad;
}

/* Top arm: [G][out][G][in][G] ending at RAMTOP. Odd alignments on purpose:
   the input ends 3 bytes short of a word, the output 1. */
static int gateTop(const struct Impl *im, const struct Stream *s) {
    uint8_t *top = (uint8_t *)RAMTOP;
    uint8_t *inEnd = top - GUARD - 3;
    uint8_t *in = inEnd - s->complen;
    uint8_t *outEnd = in - GUARD - 1;
    uint8_t *out = outEnd - s->rawlen;
    uint8_t *lo = out - GUARD;
    if (lo < __bss_end + 64 || (uint32_t)(top - lo) > sizeof(s_save)) return -1;
    /* Whatever lives up there (the loader's stack frames at least) is saved
       and put back with interrupts off for the whole window. */
    enter();
    memcpy(s_save, lo, top - lo);
    memset(lo, GUARDBYTE, top - lo);
    memcpy(in, s->comp, s->complen);
    im->fn(in, inEnd, out);
    int bad = 0;
    if (memcmp(out, s->raw, s->rawlen)) bad |= 1;
    if (!guardOk(lo, GUARD) || !guardOk(outEnd, in - outEnd)) bad |= 2;
    if (memcmp(in, s->comp, s->complen)) bad |= 4;
    if (!guardOk(inEnd, top - inEnd)) bad |= 8;
    memcpy(lo, s_save, top - lo);
    leave();
    return bad;
}

/* In-place arm: [G][buf = rawlen + margin, comp at its tail][G]. */
static int gateInplace(const struct Impl *im, const struct Stream *s, uint32_t margin) {
    uint32_t sz = s->rawlen + margin;
    if (s->complen > sz) return 16;
    uint8_t *buf = s_work + GUARD;
    memset(s_work, GUARDBYTE, sizeof(s_work));
    memcpy(buf + sz - s->complen, s->comp, s->complen);
    enter();
    im->fn(buf + sz - s->complen, buf + sz, buf);
    leave();
    int bad = 0;
    if (memcmp(buf, s->raw, s->rawlen)) bad |= 1;
    if (!guardOk(s_work, GUARD) || !guardOk(buf + sz, GUARD)) bad |= 2;
    return bad;
}

static uint32_t s_h25;

/* System clocks per 25 hblanks, interrupts off. */
static void calibrate(void) {
    uint32_t best = 0;
    for (int r = 0; r < 5; r++) {
        enter();
        uint16_t h = COUNTERS[1].value;
        while (COUNTERS[1].value == h);
        uint16_t c0 = COUNTERS[2].value;
        h = COUNTERS[1].value;
        while ((uint16_t)(COUNTERS[1].value - h) < 25);
        uint16_t c1 = COUNTERS[2].value;
        leave();
        uint32_t d = (uint16_t)(c1 - c0);
        if (d > best) best = d;
        ramsyscall_printf("lz4duel: calib run %d clocks/25hblank %d\n", r, d);
    }
    s_h25 = best;
}

/* The counter 2 delta is exact mod 65536; the hblank delta picks the wrap. */
static uint32_t cycles(uint32_t c2, uint32_t hb) {
    uint32_t est = hb * s_h25 / 25;
    uint32_t k = (est - c2 + 32768) >> 16;
    if (est + 32768 < c2) k = 0;
    return c2 + (k << 16);
}

static void sort(uint32_t *v, int n) {
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && v[j - 1] > v[j]; j--) {
            uint32_t t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
}

static void bench(void) {
    const uint8_t *p = g_corpus;
    uint32_t count = rd32(p);
    p += 4;
    static struct Stream streams[160];
    if (count > 160) {
        ramsyscall_printf("lz4duel: corpus too large\n");
        s_exit = 2;
        return;
    }
    for (uint32_t i = 0; i < count; i++) p = parse(p, &streams[i]);
    ramsyscall_printf("lz4duel: streams %d impls %d end %08x\n", count, NIMPLS, (uint32_t)__bss_end);

    COUNTERS[1].mode = TM_CLK_EXTERNAL;
    COUNTERS[2].mode = 0;
    calibrate();

    for (unsigned k = 0; k < NIMPLS; k++) {
        const struct Impl *im = &s_impls[k];
        int fails = 0, ctlCaught = 0, ctlRun = 0;
        for (uint32_t i = 0; i < count; i++) {
            const struct Stream *s = &streams[i];
            int n = gateNormal(im, s);
            int t = gateTop(im, s);
            int ip = gateInplace(im, s, s->margin);
            if (n || t || ip) {
                fails++;
                ramsyscall_printf("lz4duel: FAIL %s %s normal %d top %d inplace %d\n", im->name, s->name, n, t, ip);
            }
            if (im->baseline && s->margin > 0 && s->complen <= s->rawlen + s->margin - 1) {
                ctlRun++;
                if (gateInplace(im, s, s->margin - 1)) ctlCaught++;
            }
        }
        ramsyscall_printf("lz4duel: gate %s %s fails %d of %d streams", im->name, fails ? "FAILED" : "PASSED", fails,
                          count);
        if (im->baseline) ramsyscall_printf(" margin-1 control caught %d of %d", ctlCaught, ctlRun);
        ramsyscall_printf("\n");
        if (fails) s_exit = 1;
    }

    uint32_t totals[NIMPLS];
    for (unsigned k = 0; k < NIMPLS; k++) totals[k] = 0;
    for (uint32_t i = 0; i < count; i++) {
        const struct Stream *s = &streams[i];
        if (!(s->flags & 1)) continue;
        uint32_t v[NIMPLS][RUNS];
        uint32_t hbs[NIMPLS][RUNS];
        memcpy(s_src, s->comp, s->complen);
        for (int r = 0; r < RUNS; r++) {
            for (unsigned k = 0; k < NIMPLS; k++) {
                memset(s_work, 0, sizeof(s_work));
                uint32_t hb;
                enter();
                uint32_t c2 = benchTime(s_impls[k].fn, s_src, s_src + s->complen, s_work + GUARD, &hb);
                leave();
                v[k][r] = cycles(c2, hb);
                hbs[k][r] = hb;
                if (memcmp(s_work + GUARD, s->raw, s->rawlen)) {
                    ramsyscall_printf("lz4duel: TIMED OUTPUT MISMATCH %s %s\n", s_impls[k].name, s->name);
                    s_exit = 1;
                }
            }
        }
        for (unsigned k = 0; k < NIMPLS; k++) {
            ramsyscall_printf("lz4duel: runs %s %s", s_impls[k].name, s->name);
            for (int r = 0; r < RUNS; r++) ramsyscall_printf(" %d/%d", v[k][r], hbs[k][r]);
            sort(v[k], RUNS);
            uint32_t med = v[k][RUNS / 2];
            totals[k] += med;
            ramsyscall_printf(" median %d\n", med);
        }
    }
    for (unsigned k = 0; k < NIMPLS; k++)
        ramsyscall_printf("lz4duel: TOTAL %s %d cycles\n", s_impls[k].name, totals[k]);
}

int main(void) {
    benchOnStack(bench, s_stack + 4096);
    ramsyscall_printf("lz4duel: done exit %d\n", s_exit);
    exitWith(s_exit);
}
