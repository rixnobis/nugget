/* Correctness pre-check under qemu-mipsel: every corpus stream through every
   implementation, 16 alignment pairs with guards, and in-place at the bar.
   Prints one line per failure and a summary; exit code = failure count. */
#include <stdint.h>

typedef void (*Fn)(const void *, const void *, void *);
void lz4_decompress_block(const void *, const void *, void *);
QCANDS_DECL
static const struct { const char *name; Fn fn; } impls[] = {{"baseline", lz4_decompress_block}, QCANDS};

void qwrite(const char *, uint32_t);
extern const uint8_t g_corpus[];
static uint8_t work[300000], src[140000];

static void puts_(const char *s) { uint32_t n = 0; while (s[n]) n++; qwrite(s, n); }
static void putn(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); puts_(b + i); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) { while (n--) *d++ = *s++; }
static void set(uint8_t *d, uint8_t c, uint32_t n) { while (n--) *d++ = c; }
static int cmp(const uint8_t *a, const uint8_t *b, uint32_t n) { while (n--) if (*a++ != *b++) return 1; return 0; }
static int guard(const uint8_t *p, uint32_t n) { while (n--) if (*p++ != 0xa5) return 1; return 0; }

int qmain(void) {
    const uint8_t *p = g_corpus;
    uint32_t count = rd32(p), fails = 0;
    p += 4;
    for (uint32_t i = 0; i < count; i++) {
        const char *name = (const char *)p;
        uint32_t raw = rd32(p + 28), comp = rd32(p + 32), margin = rd32(p + 36);
        const uint8_t *rp = p + 40, *cp = rp + ((raw + 3) & ~3);
        p = cp + ((comp + 3) & ~3);
        for (unsigned k = 0; k < sizeof(impls) / sizeof(impls[0]); k++) {
            int bad = 0;
            for (int so = 0; so < 4; so++)
                for (int d = 0; d < 4; d++) {
                    cpy(src + so, cp, comp);
                    set(work, 0xa5, raw + 600);
                    impls[k].fn(src + so, src + so + comp, work + 256 + d);
                    if (cmp(work + 256 + d, rp, raw) || guard(work, 256 + d) || guard(work + 256 + d + raw, 256)) bad |= 1;
                }
            uint32_t sz = raw + margin;
            if (comp <= sz) {
                set(work, 0xa5, sz + 512);
                cpy(work + 256 + sz - comp, cp, comp);
                impls[k].fn(work + 256 + sz - comp, work + 256 + sz, work + 256);
                if (cmp(work + 256, rp, raw) || guard(work, 256) || guard(work + 256 + sz, 256)) bad |= 2;
            }
            if (bad) {
                fails++;
                puts_("FAIL "); puts_(impls[k].name); puts_(" "); puts_(name); puts_(" arms "); putn(bad); puts_("\n");
            }
        }
    }
    puts_("qtest: streams "); putn(count); puts_(" failures "); putn(fails); puts_("\n");
    return fails;
}
