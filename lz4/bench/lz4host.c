/* Host side of the lz4 duel bench, built against the unmodified ../lz4.c.
   lz4host check RAW COMP   exact-size heap buffers (run under ASan/UBSan),
                            exit 0 when the output matches RAW.
   lz4host margin RAW COMP  smallest in-place margin M from which every larger
                            margin decompresses correctly with COMP at the tail
                            of a RAW+M buffer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../lz4.h"

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) exit(3);
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *p = malloc(*len ? *len : 1);
    if (*len && fread(p, 1, *len, f) != *len) exit(3);
    fclose(f);
    return p;
}

/* A margin too small corrupts the input under the decoder, which then reads
   or loops past the buffer; each attempt runs in its own process. */
static int inplaceOnce(const unsigned char *raw, size_t rawlen, const unsigned char *comp, size_t complen, size_t m) {
    size_t sz = rawlen + m;
    if (complen > sz) return 0;
    unsigned char *buf = malloc(sz ? sz : 1);
    memset(buf, 0xa5, sz);
    memcpy(buf + sz - complen, comp, complen);
    lz4_decompress_block(buf + sz - complen, buf + sz, buf);
    int ok = memcmp(buf, raw, rawlen) == 0;
    free(buf);
    return ok;
}

static int inplace(const unsigned char *raw, size_t rawlen, const unsigned char *comp, size_t complen, size_t m) {
    pid_t pid = fork();
    if (pid == 0) {
        alarm(5);
        _exit(inplaceOnce(raw, rawlen, comp, complen, m) ? 0 : 1);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    size_t rawlen, complen;
    unsigned char *raw = slurp(argv[2], &rawlen);
    unsigned char *comp = slurp(argv[3], &complen);
    if (!strcmp(argv[1], "check")) {
        unsigned char *in = malloc(complen);
        memcpy(in, comp, complen);
        unsigned char *out = malloc(rawlen ? rawlen : 1);
        lz4_decompress_block(in, in + complen, out);
        int ok = memcmp(out, raw, rawlen) == 0;
        printf("%s\n", ok ? "OK" : "MISMATCH");
        return ok ? 0 : 1;
    }
    if (!strcmp(argv[1], "margin")) {
        /* Overlap conflicts only shrink as M grows; a success below the
           conflict-free margin is a coincidence of overwritten values. The
           bar is the start of the first run of 64 consecutive successes. */
        size_t m = 0, run = 0, start = 0;
        for (; run < 64; m++) {
            if (inplace(raw, rawlen, comp, complen, m)) {
                if (!run) start = m;
                run++;
            } else {
                run = 0;
            }
        }
        printf("%zu\n", start);
        return 0;
    }
    return 2;
}
