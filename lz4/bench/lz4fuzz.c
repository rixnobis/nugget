/* Random well-formed LZ4 blocks (lz4gen.h) against ../lz4.c, exact-size heap
   buffers, built with ASan/UBSan.
   lz4fuzz SEED COUNT [DUMPDIR [BARS]]
   Prints the corpus digest (compare with the qemu and PS1 runs). DUMPDIR gets
   every 997th block as raw/lz4 pairs for a cross-check with the reference
   decoder. BARS (a path) gets "index margin" for every 101st block: the
   baseline's in-place bar, computed as in lz4host.c. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../lz4.h"

static uint8_t s_rawbuf[192 * 1024], s_compbuf[192 * 1024];
#define GEN_RAW s_rawbuf
#define GEN_COMP s_compbuf
#include "lz4gen.h"

static int inplaceOnce(uint32_t m) {
    uint32_t sz = s_rawlen + m;
    if (s_complen > sz) return 0;
    uint8_t *buf = malloc(sz ? sz : 1);
    memset(buf, 0xa5, sz);
    memcpy(buf + sz - s_complen, s_compbuf, s_complen);
    lz4_decompress_block(buf + sz - s_complen, buf + sz, buf);
    int ok = memcmp(buf, s_rawbuf, s_rawlen) == 0;
    free(buf);
    return ok;
}

static int inplace(uint32_t m) {
    pid_t pid = fork();
    if (pid == 0) {
        alarm(5);
        _exit(inplaceOnce(m) ? 0 : 1);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static uint32_t bar(void) {
    uint32_t m = 0, run = 0, start = 0;
    for (; run < 64; m++) {
        if (inplace(m)) {
            if (!run) start = m;
            run++;
        } else {
            run = 0;
        }
    }
    return start;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    for (uint64_t s = 1; s <= 5; s++) {
        uint64_t v = s * 0x9e3779b97f4a7c15ull + 1;
        if ((uint32_t)v != s_genSeeds[s][0] || (uint32_t)(v >> 32) != s_genSeeds[s][1]) {
            printf("seed table wrong at %d: want %08x %08x\n", (int)s, (uint32_t)v, (uint32_t)(v >> 32));
            return 3;
        }
    }
    int seed = atoi(argv[1]);
    unsigned long count = strtoul(argv[2], 0, 0);
    const char *dump = argc > 3 && argv[3][0] ? argv[3] : 0;
    FILE *bars = argc > 4 ? fopen(argv[4], "w") : 0;
    genSeed(seed);
    uint32_t digest = 2166136261u;
    unsigned long dumped = 0;
    for (unsigned long n = 0; n < count; n++) {
        gen();
        digest = genDigest(digest);
        uint8_t *in = malloc(s_complen);
        memcpy(in, s_compbuf, s_complen);
        uint8_t *out = malloc(s_rawlen ? s_rawlen : 1);
        lz4_decompress_block(in, in + s_complen, out);
        if (memcmp(out, s_rawbuf, s_rawlen)) {
            printf("MISMATCH at block %lu rawlen %u complen %u\n", n, s_rawlen, s_complen);
            return 1;
        }
        free(in);
        free(out);
        if (dump && n % 997 == 0) {
            char p[512];
            snprintf(p, sizeof(p), "%s/%06lu.raw", dump, dumped);
            FILE *f = fopen(p, "wb");
            fwrite(s_rawbuf, 1, s_rawlen, f);
            fclose(f);
            snprintf(p, sizeof(p), "%s/%06lu.lz4", dump, dumped++);
            f = fopen(p, "wb");
            fwrite(s_compbuf, 1, s_complen, f);
            fclose(f);
        }
        if (bars && n % 101 == 0) fprintf(bars, "%lu %u\n", n, bar());
    }
    if (bars) fclose(bars);
    printf("OK seed %d blocks %lu digest %08x\n", seed, count, digest);
    return 0;
}
