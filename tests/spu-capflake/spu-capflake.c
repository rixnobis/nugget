/*
 * Board#943 probe: why does run_voice1_with_sample's capture leave a run of
 * zeros at the end of the golden window in some boots and not others?
 *
 * Per iteration:
 *   A  writer phase: arm IRQ9 at 0x0800 (capture ring index 0), wait for the
 *      IRQ, then time the next SPUSTAT bit 11 1->0 edge. Same for 0x0A00 and
 *      the 0->1 edge. Reported in output samples (timer2 sysclk/8, 96 ticks per
 *      sample).
 *   B  run_voice1_with_sample verbatim (sine, pitch 0x1000), plus timestamps:
 *      key-on write -> ENVX nonzero, key-on write -> the flip the read waits
 *      on. Then the zero runs in the ring, then a second read one lap later.
 */

#include "common/hardware/dma.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/counters.h"
#include "common/hardware/pcsxhw.h"
#include "common/hardware/spu.h"
#include "common/syscalls/syscalls.h"

#include <stdint.h>

#define SPU_DELAY (*(volatile uint32_t *)0xbf801014)
#define SPU_UPLOAD_ADDR 0x1080
#define ITER 6

static uint8_t s_upload[128] __attribute__((aligned(4)));
static uint16_t s_capture[512] __attribute__((aligned(4)));
static uint16_t s_capture2[512] __attribute__((aligned(4)));

static const uint8_t kAdpcmSine[64] __attribute__((aligned(4))) = {
    0x00, 0x06, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
    0x00, 0x03, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
};

static inline uint16_t now(void) { return COUNTERS[2].value; }
// ticks of sysclk/8; 96 ticks = one 44.1 kHz sample (768 cycles)
static inline int ticks(uint16_t a, uint16_t b) { return (uint16_t)(b - a); }

static void spu_dma_sync(uint32_t spuByteAddr, uint32_t cpuAddr, uint32_t bytes, int isRead) {
    const uint16_t tsa = (uint16_t)(spuByteAddr >> 3);
    const uint16_t modeBits = isRead ? 0x0030 : 0x0020;
    const uint32_t delay = isRead ? 0x22000000 : 0x20000000;
    const uint32_t chcr = isRead ? 0x01000200 : 0x01000201;

    SPU_RAM_DTA = tsa;
    for (int i = 0; i < 0xF01 && (SPU_RAM_DTA & 0xffff) != tsa; i++);
    SPU_CTRL = (SPU_CTRL & ~0x0030) | modeBits;
    for (volatile int i = 0; i < 60; i++);
    for (int i = 0; i < 0xF01 && (SPU_STATUS & 0x30) != modeBits; i++);
    SPU_DELAY = (SPU_DELAY & 0xf0ffffff) | delay;

    uint32_t blocks = (bytes >> 6) + ((bytes & 0x3f) ? 1 : 0);
    DMA_CTRL[DMA_SPU].MADR = cpuAddr & 0x1fffffff;
    DMA_CTRL[DMA_SPU].BCR = (blocks << 16) | 0x10;
    DMA_CTRL[DMA_SPU].CHCR = chcr;
    while ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0) __asm__ volatile("");

    SPU_CTRL = (SPU_CTRL & ~0x0030);
    for (volatile int i = 0; i < 60; i++);
}

__attribute__((noinline)) static void spu_busy_wait(unsigned cycles) {
    unsigned n = cycles / 3;
    if (n == 0) return;
    __asm__ volatile(
        "1: addiu %0, %0, -1 \n"
        "   bnez  %0, 1b     \n"
        "   nop              \n"
        : "+r"(n)
        :
        : "memory");
}

static inline void flip(void) {
    while (!(SPU_STATUS & 0x0800));
    while ((SPU_STATUS & 0x0800));
}

static void spu_reset_quiet(void) {
    DPCR |= 0x000b0000;
    SPU_CTRL = 0;
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_REVERB_LEFT = 0;
    SPU_REVERB_RIGHT = 0;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    SPU_PITCH_MOD_LOW = 0;
    SPU_PITCH_MOD_HIGH = 0;
    SPU_NOISE_EN_LOW = 0;
    SPU_NOISE_EN_HIGH = 0;
    SPU_REVERB_EN_LOW = 0;
    SPU_REVERB_EN_HIGH = 0;
    SPU_REVERB_ADDR = 0xffff;
    SPU_RAM_DTC = 4;
    SPU_CTRL = 0x8000;
}

static int irq_wait(uint16_t addr) {
    SPU_IRQ_ADDR = addr >> 3;
    SPU_CTRL &= ~0x0040;
    SPU_CTRL |= 0x0040;
    for (int i = 0; i < 2000000; i++) {
        if (SPU_STATUS & 0x0040) return 1;
    }
    return 0;
}

static void phase_probe(int it) {
    spu_reset_quiet();
    SPU_CTRL = 0x8000 | 0x4000;
    int ok0 = irq_wait(0x0800);
    uint16_t t0 = now();
    while ((SPU_STATUS & 0x0800)) if (ticks(t0, now()) > 60000) break;
    while (!(SPU_STATUS & 0x0800)) if (ticks(t0, now()) > 60000) break;
    while ((SPU_STATUS & 0x0800)) if (ticks(t0, now()) > 60000) break;
    int fall = ticks(t0, now());
    // re-measure falling edge relative to the IRQ without the leading skip:
    int ok1 = irq_wait(0x0800);
    uint16_t t1 = now();
    uint16_t s1 = SPU_STATUS;
    int okm = irq_wait(0x0A00);
    uint16_t t2 = now();
    uint16_t s2 = SPU_STATUS;
    SPU_CTRL &= ~0x0040;
    ramsyscall_printf(
        "CAPFLAKE phase it=%d irq0=%d fall_after_irq0=%d.%02d smp | irq0->irqA00=%d.%02d smp bit11@irq0=%d "
        "bit11@irqA00=%d\n",
        it, ok0 && ok1 && okm, fall / 96, (fall % 96) * 100 / 96, ticks(t1, t2) / 96,
        (ticks(t1, t2) % 96) * 100 / 96, (s1 >> 11) & 1, (s2 >> 11) & 1);
}

static void zero_runs(const char *tag, int it, const uint16_t *c) {
    int nz = 0, first = -1, runs = 0, longest = 0, longestAt = -1;
    for (int i = 0; i < 512; i++) {
        if (c[i] != 0) nz++;
        if (c[i] != 0 && first < 0) first = i;
    }
    // circular zero runs
    int start = -1;
    for (int i = 0; i < 512; i++) {
        if (c[i] != 0 && c[(i + 1) & 511] == 0) { start = (i + 1) & 511; break; }
    }
    if (start >= 0) {
        int i = start, n = 0;
        while (n < 512) {
            if (c[i & 511] == 0) {
                int s = i & 511, len = 0;
                while (len < 512 && c[i & 511] == 0) { i++; len++; n++; }
                runs++;
                if (len > longest) { longest = len; longestAt = s; }
            } else {
                i++;
                n++;
            }
        }
    }
    ramsyscall_printf("CAPFLAKE %s it=%d nz=%d first=%d zeroRuns=%d longest=%d at=%d c0=%04x c255=%04x c511=%04x\n",
                      tag, it, nz, first, runs, longest, longestAt, c[0], c[255], c[511]);
}

static void capture_probe(int it) {
    spu_reset_quiet();
    for (int i = 0; i < 64; i++) s_upload[i] = kAdpcmSine[i];
    for (int i = 64; i < 128; i++) s_upload[i] = 0xaa;
    spu_dma_sync(SPU_UPLOAD_ADDR, (uint32_t)s_upload, 128, 0);
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_VOL_MAIN_LEFT = 0x3fff;
    SPU_VOL_MAIN_RIGHT = 0x3fff;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    spu_busy_wait(800000);

    flip();
    uint16_t tEdge = now();
    SPU_VOICES[1].sampleRate = 0x1000;
    SPU_VOICES[1].sampleStartAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].sampleRepeatAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].adsrLo = 0x000f;
    SPU_VOICES[1].adsrHi = 0x1fc0;
    SPU_KEY_OFF_LOW = 0;
    SPU_KEY_OFF_HIGH = 0;
    SPU_KEY_ON_LOW = 1u << 1;
    uint16_t tKon = now();
    int envPolls = 0;
    while (SPU_VOICES[1].currentVolume == 0 && envPolls < 100000) envPolls++;
    uint16_t tEnv = now();
    flip();
    uint16_t tFlip = now();
    spu_dma_sync(0x0800, (uint32_t)s_capture, 1024, 1);
    uint16_t tRead = now();
    flip();
    spu_dma_sync(0x0800, (uint32_t)s_capture2, 1024, 1);
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;

    ramsyscall_printf(
        "CAPFLAKE timing it=%d edge->kon=%d kon->envx=%d.%02d smp (polls=%d) kon->flip=%d.%02d smp read=%d.%02d smp\n",
        it, ticks(tEdge, tKon), ticks(tKon, tEnv) / 96, (ticks(tKon, tEnv) % 96) * 100 / 96, envPolls,
        ticks(tKon, tFlip) / 96, (ticks(tKon, tFlip) % 96) * 100 / 96, ticks(tFlip, tRead) / 96,
        (ticks(tFlip, tRead) % 96) * 100 / 96);
    zero_runs("read1", it, s_capture);
    zero_runs("read2", it, s_capture2);
    int diff = 0;
    for (int i = 0; i < 512; i++) diff += s_capture[i] != s_capture2[i];
    ramsyscall_printf("CAPFLAKE diff it=%d read1_vs_read2=%d\n", it, diff);
}

asm(".pushsection .rodata\n.global g_sine\n.align 2\ng_sine:\n.incbin \"../spu/sine.test.pcm\"\n.popsection");
extern const uint8_t g_sine[];

// spu_compare_golden's alignment (first minimum), and its periodicity verdict.
static int best_first(const uint16_t *a, int *outS) {
    const uint32_t *h = (const uint32_t *)g_sine;
    const int16_t *g = (const int16_t *)(g_sine + h[1]);
    int warm = h[2] / 2, per = h[3] / 2, kept = warm + per, st = warm > 15 ? warm : 15;
    int bestS = 0, bestBad = 0x7fffffff;
    for (int s = 0; s < 512; s++) {
        int bad = 0;
        for (int i = st; i < kept; i++) bad += (int16_t)a[(s + i) & 511] != g[i];
        if (bad < bestBad) { bestBad = bad; bestS = s; }
    }
    *outS = bestS;
    for (int i = kept; i < 512; i++)
        if (a[(bestS + i) & 511] != a[(bestS + i - per) & 511]) return i + 1;
    return bestBad ? -bestBad : 0;
}

static void sweep(int it, int delaySmp) {
    spu_reset_quiet();
    for (int i = 0; i < 64; i++) s_upload[i] = kAdpcmSine[i];
    for (int i = 64; i < 128; i++) s_upload[i] = 0xaa;
    spu_dma_sync(SPU_UPLOAD_ADDR, (uint32_t)s_upload, 128, 0);
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_VOL_MAIN_LEFT = 0x3fff;
    SPU_VOL_MAIN_RIGHT = 0x3fff;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    spu_busy_wait(800000);
    flip();
    uint16_t tEdge = now();
    while (ticks(tEdge, now()) < delaySmp * 96);
    SPU_VOICES[1].sampleRate = 0x1000;
    SPU_VOICES[1].sampleStartAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].sampleRepeatAddr = SPU_UPLOAD_ADDR >> 3;
    SPU_VOICES[1].adsrLo = 0x000f;
    SPU_VOICES[1].adsrHi = 0x1fc0;
    SPU_KEY_OFF_LOW = 0;
    SPU_KEY_OFF_HIGH = 0;
    SPU_KEY_ON_LOW = 1u << 1;
    uint16_t tKon = now();
    while (SPU_VOICES[1].currentVolume == 0 && ticks(tKon, now()) < 96 * 40);
    uint16_t tEnv = now();
    flip();
    spu_dma_sync(0x0800, (uint32_t)s_capture, 1024, 1);
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    int onset = -1;
    for (int i = 1; i < 64; i++)
        if (s_capture[i] != 0) { onset = i; break; }
    int bs, r = best_first(s_capture, &bs);
    int envT = ticks(tEdge, tEnv);
    ramsyscall_printf(
        "CAPFLAKE sweep it=%d d=%d edge->envx=%d.%02d smp onset=%d c[onset]=%04x c[onset+1]=%04x bestS=%d cmp=%d\n", it,
        delaySmp, envT / 96, (envT % 96) * 100 / 96, onset, s_capture[onset], s_capture[onset + 1], bs, r);
    if (it == 0 && delaySmp == 0) {
        for (int i = 0; i < 64; i += 16) {
            ramsyscall_printf("CAPFLAKE dump %03d:", i);
            for (int j = 0; j < 16; j++) ramsyscall_printf(" %04x", s_capture[i + j]);
            ramsyscall_printf("\n");
        }
    }
}

int main() {
    enterCriticalSection();
    IMASK = 0;
    IREG = 0;
    COUNTERS[2].mode = 0x0200;  // sysclk/8, free running
    ramsyscall_printf("CAPFLAKE v2 start\n");
    for (int it = 0; it < 2; it++) {
        phase_probe(it);
        capture_probe(it);
    }
    for (int it = 0; it < 3; it++)
        for (int d = 0; d < 5; d++) sweep(it, d);
    SPU_CTRL = 0x8000;
    ramsyscall_printf("CAPFLAKE done\n");
    pcsx_exit(0);
    while (1);
}
