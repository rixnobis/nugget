/* The random well-formed corpus (lz4gen.h, seed SEED, blocks 0..COUNT-1)
   through the candidate on the PS1. Per block: source and destination at
   one of the 16 alignment pairs (picked by block index), 256-byte guards on
   both sides of the output, the output area pre-filled so an unwritten byte
   shows; output compared byte for byte with the generator's raw data. Every
   block listed in bars.h (every 101st, bar = the baseline's in-place margin
   from lz4fuzz on the host) also runs in place at that bar, through the
   candidate and the baseline. The corpus digest printed at the end must equal
   the host's (lz4fuzz SEED COUNT). Exit code 0 when nothing failed. */

#include <stdint.h>

#include "common/syscalls/syscalls.h"
#include "lz4/lz4.h"

void lz4_cand1(const void *, const void *, void *);

static uint8_t s_rawbuf[192 * 1024], s_compbuf[192 * 1024];
#define GEN_RAW s_rawbuf
#define GEN_COMP s_compbuf
#include "../lz4gen.h"
#include "bars.h"

#define GUARD 256
static uint8_t s_src[192 * 1024 + 4];
static uint8_t s_out[GUARD + 192 * 1024 + 4 + GUARD];
static uint8_t s_ip[GUARD + 200 * 1024 + GUARD];

static __attribute__((noreturn)) void exitWith(int code) {
    register int a0 asm("a0") = code;
    __asm__ volatile("break 4, 0\n" : : "r"(a0));
    __builtin_unreachable();
}

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    while (n--) *d++ = *s++;
}
static void fill(uint8_t *d, uint8_t c, uint32_t n) {
    while (n--) *d++ = c;
}
static int differs(const uint8_t *a, const uint8_t *b, uint32_t n) {
    while (n--)
        if (*a++ != *b++) return 1;
    return 0;
}
static int guardBad(const uint8_t *p, uint8_t c, uint32_t n) {
    while (n--)
        if (*p++ != c) return 1;
    return 0;
}

/* 0 = good, bit 0 = output, bit 1 = guards. */
static int normalArm(uint32_t n) {
    uint32_t so = (n >> 2) & 3, d = n & 3;
    uint8_t f = (n & 1) ? 0x5a : 0xa5;
    cpy(s_src + so, s_compbuf, s_complen);
    uint8_t *dst = s_out + GUARD + d;
    fill(s_out, f, GUARD + d + s_rawlen + GUARD);
    lz4_cand1(s_src + so, s_src + so + s_complen, dst);
    int bad = 0;
    if (differs(dst, s_rawbuf, s_rawlen)) bad |= 1;
    if (guardBad(s_out, f, GUARD + d) || guardBad(dst + s_rawlen, f, GUARD)) bad |= 2;
    return bad;
}

static int inplaceArm(void (*fn)(const void *, const void *, void *), uint32_t margin) {
    uint32_t sz = s_rawlen + margin;
    if (s_complen > sz || sz > 200 * 1024) return 4;
    uint8_t *buf = s_ip + GUARD;
    fill(s_ip, 0xa5, GUARD + sz + GUARD);
    cpy(buf + sz - s_complen, s_compbuf, s_complen);
    fn(buf + sz - s_complen, buf + sz, buf);
    int bad = 0;
    if (differs(buf, s_rawbuf, s_rawlen)) bad |= 1;
    if (guardBad(s_ip, 0xa5, GUARD) || guardBad(buf + sz, 0xa5, GUARD)) bad |= 2;
    return bad;
}

int main(void) {
    genSeed(SEED);
    uint32_t digest = 2166136261u, fails = 0, ipRun = 0, ipFail = 0, ipBaseFail = 0, maxRaw = 0;
    uint32_t nextBar = 0;
    ramsyscall_printf("lz4rand: seed %d blocks %d bars %d\n", SEED, COUNT, (int)(sizeof(s_bars) / sizeof(s_bars[0])));
    for (uint32_t n = 0; n < COUNT; n++) {
        gen();
        digest = genDigest(digest);
        if (s_rawlen > maxRaw) maxRaw = s_rawlen;
        int bad = normalArm(n);
        if (bad) {
            if (fails < 20) ramsyscall_printf("lz4rand: FAIL block %d raw %d comp %d arms %d\n", n, s_rawlen, s_complen, bad);
            fails++;
        }
        if (nextBar < sizeof(s_bars) / sizeof(s_bars[0]) && s_bars[nextBar][0] == n) {
            uint32_t m = s_bars[nextBar++][1];
            ipRun++;
            int c = inplaceArm(lz4_cand1, m);
            int b = inplaceArm(lz4_decompress_block, m);
            if (c) {
                if (ipFail < 20) ramsyscall_printf("lz4rand: INPLACE FAIL cand1 block %d margin %d arms %d\n", n, m, c);
                ipFail++;
            }
            if (b) {
                if (ipBaseFail < 20) ramsyscall_printf("lz4rand: INPLACE FAIL baseline block %d margin %d arms %d\n", n, m, b);
                ipBaseFail++;
            }
        }
        if ((n + 1) % 5000 == 0) ramsyscall_printf("lz4rand: progress %d fails %d\n", n + 1, fails);
    }
    /* Controls on the last block: a corrupted expectation and a poked guard
       must both be seen, or the comparisons above could not fail. */
    int ctl = 0;
    normalArm(COUNT - 1);
    if (s_rawlen) {
        uint8_t *dst = s_out + GUARD + ((COUNT - 1) & 3);
        s_rawbuf[s_rawlen / 2] ^= 0x40;
        if (differs(dst, s_rawbuf, s_rawlen)) ctl |= 1;
        s_rawbuf[s_rawlen / 2] ^= 0x40;
    } else {
        ctl |= 1;
    }
    s_out[3] ^= 0xff;
    if (guardBad(s_out, ((COUNT - 1) & 1) ? 0x5a : 0xa5, GUARD)) ctl |= 2;
    ramsyscall_printf("lz4rand: controls %s\n", ctl == 3 ? "ok" : "BROKEN");
    ramsyscall_printf("lz4rand: seed %d blocks %d digest %08x maxraw %d fails %d inplace %d cand1-fails %d baseline-fails %d\n", SEED,
                      COUNT, digest, maxRaw, fails, ipRun, ipFail, ipBaseFail);
    int rc = (fails || ipFail || ipBaseFail || ctl != 3) ? 1 : 0;
    ramsyscall_printf("lz4rand: done exit %d\n", rc);
    exitWith(rc);
}
