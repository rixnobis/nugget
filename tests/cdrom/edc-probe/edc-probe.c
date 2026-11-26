/*

MIT License

Copyright (c) 2026 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

/*
 * EDC/ECC probe: what the CD-ROM controller does with a data sector whose
 * EDC/ECC does not verify. Needs the disc from cdrom/common/create-test-iso.lua,
 * whose arm table (LBA 100000 on) says how each BAD sector is built. Each arm
 * is 8 sectors, the BAD one third; the 8 after it are counter fillers.
 *
 * For every arm and Setmode 80h, 00h and A0h: Setloc to the arm start, ReadN,
 * then up to 10 interrupts, each waited for at most 3 s. Every INT1 sector is
 * read by the CPU and identified from its payload (LBA in the first 4 bytes),
 * compared with the pattern the disc should carry there, and for 2340-byte
 * reads its header, subheader and EDC field are kept too. After an INT5,
 * GetlocL and Nop. Then Pause, to its completion. Events are buffered and
 * printed once the drive is paused, so the console's speed cannot change the
 * timings. Probe only: logs what happened, no verdict, exits 0.
 *
 * One line per event:
 *   EDC arm=<A-G> mode=<hex> ev=<n> ph=<phase> int=<1-5> resp=<bytes> t=<us> ...
 * ph: M Setmode, S Setloc, R the ReadN and the interrupts after it, L GetlocL,
 * N Nop, P Pause, X anything within 200 ms after the Pause completed, I the
 * Init at start. t is hblank-timer microseconds since the command of that
 * phase was written (since the ReadN for R, L and N, the Pause for X). A wait that runs out
 * prints TIMEOUT instead of int=. INT1 lines add the payload LBA, MATCH or
 * MISMATCH against the pattern expected at that LBA (with the first differing
 * offset and which known pattern the data is: N normal, A alternate, F
 * counter filler, ? none), checksums of the delivered and expected 2048
 * bytes, and for 2340-byte reads hdr=MM:SS:FF.mode, the subheader, the EDC
 * field and a checksum of the bytes after the 2048 data bytes.
 */

#include <stdint.h>

#include "common/hardware/cdrom.h"
#include "common/hardware/counters.h"
#include "common/hardware/hwregs.h"
#include "common/hardware/irq.h"
#include "common/syscalls/syscalls.h"

#define ARM_START 100000u
#define ARM_STRIDE 16u
#define ARM_COUNT 7u
#define ARM_LENGTH 8u
#define READ_EVENTS 10
#define MAX_EVENTS 40
#define WAIT_US 3000000u
#define US_PER_HBLANK 64u

static uint32_t s_lastHSync, s_time;

static void initializeTime(void) {
    s_lastHSync = COUNTERS[1].value & 0xffff;
    s_time = 0;
}

// Called at least every few ms while waiting, far inside the 16-bit wrap.
static uint32_t updateTime(void) {
    uint32_t h = COUNTERS[1].value & 0xffff;
    if (h < s_lastHSync) h += 0x10000;
    s_time += (h - s_lastHSync) * US_PER_HBLANK;
    s_lastHSync = h & 0xffff;
    return s_time;
}

typedef struct {
    uint32_t t, lba, sum, exp, edc, tail;
    int32_t diff;
    uint16_t got;
    uint8_t phase, cause, respLen, pat;
    uint8_t resp[16];
    uint8_t hdr[4], sub[8];
} Event;

static Event s_events[MAX_EVENTS];
static unsigned s_count, s_dropped;
static uint8_t s_sector[2340];
static unsigned s_sectorSize;

// The disc's arm pattern, as create-test-iso.lua writes it: the LBA little-endian in bytes 0-3,
// then (k * 0x9d + lba * 0x3b + (k >> 8) * 0x11) & 0xff, xored with 0xa5 in the alternate pattern.
// Counter fillers carry the LBA in bytes 0-2 and zeros.
static int isArmSector(uint32_t lba) {
    if (lba < ARM_START || lba >= ARM_START + ARM_STRIDE * ARM_COUNT) return 0;
    return (lba - ARM_START) % ARM_STRIDE < ARM_LENGTH;
}

static inline uint32_t rot(uint32_t s, uint8_t v) { return ((s << 1) | (s >> 31)) + v; }

// One pass over the 2048 data bytes, cheap enough to finish well inside a sector time at double
// speed, so the drive is never left waiting on a full buffer by the probe itself.
static void analyze(Event* e) {
    unsigned off = s_sectorSize == 2340 ? 12 : 0;
    e->got = s_sectorSize;
    if (s_sectorSize < off + 2048) return;
    const uint8_t* d = s_sector + off;
    uint32_t lba = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24);
    int arm = isArmSector(lba);
    uint32_t sum = 0, exp = 0;
    for (unsigned k = 0; k < 4; k++) {
        sum = rot(sum, d[k]);
        exp = rot(exp, d[k]);
    }
    int32_t diff = -1;
    int normal = 1, alt = 1, filler = 1;
    uint32_t acc = 4 * 0x9d + lba * 0x3b;
    for (unsigned k = 4; k < 2048; k++, acc += 0x9d) {
        if ((k & 0xff) == 0) acc += 0x11;
        uint8_t v = acc & 0xff, x = d[k];
        normal &= x == v;
        alt &= x == (v ^ 0xa5);
        filler &= x == 0;
        uint8_t want = arm ? v : 0;
        sum = rot(sum, x);
        exp = rot(exp, want);
        if (diff < 0 && x != want) diff = k;
    }
    e->lba = lba;
    e->sum = sum;
    e->exp = exp;
    e->diff = diff;
    e->pat = normal ? 'N' : alt ? 'A' : filler ? 'F' : '?';
    if (off) {
        for (unsigned i = 0; i < 4; i++) e->hdr[i] = s_sector[i];
        for (unsigned i = 0; i < 8; i++) e->sub[i] = s_sector[4 + i];
        unsigned at = (s_sector[6] & 0x20) ? 2336 : 2060;
        e->edc = s_sector[at] | (s_sector[at + 1] << 8) | (s_sector[at + 2] << 16) | ((uint32_t)s_sector[at + 3] << 24);
        uint32_t tail = 0;
        for (unsigned i = 2060; i < 2340; i++) tail = rot(tail, s_sector[i]);
        e->tail = tail;
    }
}

static void readSector(unsigned want) {
    s_sectorSize = 0;
    CDROM_REG0 = 0;
    CDROM_REG3 = 0x80;
    uint32_t t0 = updateTime();
    while ((CDROM_REG0 & 0x40) == 0) {
        if (updateTime() - t0 > 10000) break;
    }
    while ((CDROM_REG0 & 0x40) && s_sectorSize < want) s_sector[s_sectorSize++] = CDROM_REG2;
    CDROM_REG3 = 0;
}

// Waits for the next interrupt and records it. Returns its cause, 0 on timeout.
// Waits up to `wait` us; a drain (phase X) that times out leaves no event behind.
static uint8_t waitEventFor(uint8_t phase, unsigned sectorSize, uint32_t wait) {
    Event scratch;
    Event* e = s_count < MAX_EVENTS ? &s_events[s_count++] : (s_dropped++, &scratch);
    __builtin_memset(e, 0, sizeof(*e));
    e->phase = phase;
    e->diff = -1;
    uint32_t start = updateTime(), now;
    do {
        now = updateTime();
    } while ((IREG & IRQ_CDROM) == 0 && now - start <= wait);
    e->t = now;
    if ((IREG & IRQ_CDROM) == 0) {
        if (phase == 'X' && e != &scratch) s_count--;
        return 0;
    }
    IREG &= ~IRQ_CDROM;
    CDROM_REG0 = 1;
    uint8_t cause = CDROM_REG3_UC;
    if (cause & 7) {
        CDROM_REG0 = 1;
        CDROM_REG3 = 7;
    }
    if (cause & 0x18) {
        CDROM_REG0 = 1;
        CDROM_REG3 = cause & 0x18;
    }
    cause &= 7;
    e->cause = cause;
    while ((CDROM_REG0 & 0x20) && e->respLen < 16) e->resp[e->respLen++] = CDROM_REG1;
    if (cause == 1) {
        readSector(sectorSize);
        analyze(e);
    }
    return cause;
}

static uint8_t waitEvent(uint8_t phase, unsigned sectorSize) { return waitEventFor(phase, sectorSize, WAIT_US); }

static uint8_t command(uint8_t cmd, uint8_t phase, const uint8_t* params, unsigned n) {
    CDROM_REG0 = 0;
    for (unsigned i = 0; i < n; i++) CDROM_REG2 = params[i];
    initializeTime();
    CDROM_REG1 = cmd;
    return waitEvent(phase, 0);
}

static uint8_t itob(unsigned i) { return (i / 10) * 16 + (i % 10); }

static void printEvents(char arm, uint8_t mode) {
    for (unsigned i = 0; i < s_count; i++) {
        const Event* e = &s_events[i];
        ramsyscall_printf("EDC arm=%c mode=%02x ev=%i ph=%c ", arm, mode, i, e->phase);
        if (e->cause == 0) {
            ramsyscall_printf("TIMEOUT t=%u\n", e->t);
            continue;
        }
        ramsyscall_printf("int=%i resp=", e->cause);
        for (unsigned j = 0; j < e->respLen; j++) ramsyscall_printf(j ? ".%02x" : "%02x", e->resp[j]);
        if (e->respLen == 0) ramsyscall_printf("-");
        ramsyscall_printf(" t=%u", e->t);
        if (e->cause == 1) {
            if (e->got < 2048) {
                ramsyscall_printf(" bytes=%i SHORT\n", e->got);
                continue;
            }
            ramsyscall_printf(" lba=%u", e->lba);
            if (e->diff < 0) {
                ramsyscall_printf(" MATCH");
            } else {
                ramsyscall_printf(" MISMATCH off=%i", e->diff);
            }
            ramsyscall_printf(" pat=%c sum=%08x exp=%08x", e->pat, e->sum, e->exp);
            if (e->got == 2340) {
                ramsyscall_printf(" hdr=%02x:%02x:%02x.%02x sub=%02x%02x%02x%02x.%02x%02x%02x%02x edc=%08x tail=%08x",
                                  e->hdr[0], e->hdr[1], e->hdr[2], e->hdr[3], e->sub[0], e->sub[1], e->sub[2],
                                  e->sub[3], e->sub[4], e->sub[5], e->sub[6], e->sub[7], e->edc, e->tail);
            } else if (e->got != 2048) {
                ramsyscall_printf(" bytes=%i", e->got);
            }
        }
        ramsyscall_printf("\n");
    }
    if (s_dropped) ramsyscall_printf("EDC arm=%c mode=%02x dropped=%i\n", arm, mode, s_dropped);
}

static void runArm(unsigned arm, uint8_t mode) {
    unsigned size = (mode & 0x20) ? 2340 : 2048;
    uint32_t lba = ARM_START + ARM_STRIDE * arm + 150;
    uint8_t loc[3] = {itob(lba / 4500), itob(lba / 75 % 60), itob(lba % 75)};
    s_count = s_dropped = 0;
    command(CDL_SETMODE, 'M', &mode, 1);
    command(CDL_SETLOC, 'S', loc, 3);

    CDROM_REG0 = 0;
    initializeTime();
    CDROM_REG1 = CDL_READN;
    for (unsigned n = 0; n < READ_EVENTS; n++) {
        uint8_t c = waitEvent('R', size);
        if (c == 0) break;
        if (c == 5) {
            // GetlocL and Nop answers, with any sector that arrives first.
            static const uint8_t helpers[2][2] = {{CDL_GETLOCL, 'L'}, {CDL_NOP, 'N'}};
            for (unsigned h = 0; h < 2; h++) {
                CDROM_REG0 = 0;
                CDROM_REG1 = helpers[h][0];
                for (unsigned k = 0; k < 4; k++) {
                    uint8_t hc = waitEvent(helpers[h][1], size);
                    if (hc != 1) break;
                }
            }
        }
    }

    CDROM_REG0 = 0;
    initializeTime();
    CDROM_REG1 = CDL_PAUSE;
    // Sectors can keep arriving until the Pause lands; take them all, up to its completion.
    for (unsigned k = 0; k < MAX_EVENTS; k++) {
        uint8_t c = waitEvent('P', size);
        if (c == 0 || c == 2 || c == 5) break;
    }
    // Anything still coming within 200 ms after that, so the next arm starts quiet.
    for (unsigned k = 0; k < MAX_EVENTS; k++) {
        if (waitEventFor('X', size, 200000) == 0) break;
    }
    printEvents("ABCDEFG"[arm], mode);
}

int main(void) {
    int wasEnabled = enterCriticalSection();
    uint16_t oldMode = COUNTERS[1].mode;
    COUNTERS[1].mode = 0x0100;
    SBUS_DEV5_CTRL = 0x20943;
    SBUS_COM_CTRL = 0x132c;
    uint32_t oldIMASK = IMASK;
    IMASK = IRQ_CDROM;

    ramsyscall_printf("EDC-PROBE START base=%u stride=%u arms=%u modes=80,00,a0\n", ARM_START, ARM_STRIDE, ARM_COUNT);
    CDROM_REG0 = 1;
    CDROM_REG3 = 0x1f;
    CDROM_REG0 = 1;
    CDROM_REG2 = 0x1f;
    s_count = s_dropped = 0;
    command(CDL_INIT, 'I', 0, 0);
    waitEvent('I', 0);
    printEvents('-', 0);

    static const uint8_t modes[3] = {0x80, 0x00, 0xa0};
    for (unsigned m = 0; m < 3; m++) {
        for (unsigned arm = 0; arm < ARM_COUNT; arm++) runArm(arm, modes[m]);
    }
    ramsyscall_printf("EDC-PROBE DONE\n");

    IMASK = oldIMASK;
    COUNTERS[1].mode = oldMode;
    if (wasEnabled) leaveCriticalSection();
    return 0;
}
