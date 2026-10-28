#!/usr/bin/env python3
# Builds corpus.bin for the lz4 duel bench.
# Streams: 3 timed payloads (LZ4 HC level 12, one block each), then the edge
# corpus (hand-built sequences plus reference-compressor output). Every stream
# is checked against the reference decoder (lz4.block.decompress) and against
# ../lz4.c under ASan (./lz4host check); its in-place margin bar comes from
# ./lz4host margin.
# Layout: [u32 count] then per stream
#   [char name[24]][u32 flags][u32 rawlen][u32 complen][u32 margin]
#   [raw, padded to 4][comp, padded to 4]
# flags bit 0 = timed payload.
import os, random, struct, subprocess, sys, tempfile
import lz4.block

HERE = os.path.dirname(os.path.abspath(__file__))


def ext(n):
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return out


def encode(seqs, last):
    """seqs: list of (literals, offset, matchlen); last: final literals."""
    out = bytearray()
    for lit, off, ml in seqs:
        assert ml >= 4 and 1 <= off <= 65535
        ln, mn = len(lit), ml - 4
        out.append((min(ln, 15) << 4) | min(mn, 15))
        if ln >= 15:
            out += ext(ln - 15)
        out += lit
        out += struct.pack('<H', off)
        if mn >= 15:
            out += ext(mn - 15)
    ln = len(last)
    out.append(min(ln, 15) << 4)
    if ln >= 15:
        out += ext(ln - 15)
    out += last
    return bytes(out)


def simulate(seqs, last):
    raw = bytearray()
    for lit, off, ml in seqs:
        raw += lit
        for _ in range(ml):
            raw.append(raw[-off])
    raw += last
    return bytes(raw)


def spec_ok(seqs, last, rawlen):
    # End-of-block rules: last 5 bytes are literals, last match starts at
    # least 12 bytes before the end.
    if not seqs:
        return True
    pos = 0
    for lit, off, ml in seqs:
        pos += len(lit)
        start = pos
        assert off <= pos, 'offset reaches before the block'
        pos += ml
    return len(last) >= 5 and start <= rawlen - 12


rng = random.Random(0x1722)


def rb(n):
    return bytes(rng.randrange(256) for _ in range(n))


streams = []


def add(name, raw, comp, timed=False):
    ref = lz4.block.decompress(comp, uncompressed_size=len(raw)) if raw else (b'' if comp == b'\x00' else None)
    assert ref == raw, name
    streams.append((name, raw, comp, timed))


def hand(name, seqs, last):
    raw = simulate(seqs, last)
    assert spec_ok(seqs, last, len(raw)), name
    add(name, raw, encode(seqs, last))


def hc(raw):
    return lz4.block.compress(raw, mode='high_compression', compression=12, store_size=False)


# Timed payloads.
for name, path in (('tim:city', 'data/city.tim'), ('exe:torus', 'data/torus.ps-exe'),
                   ('txt:cdromdrive', 'data/cdromdrive-64k.md')):
    raw = open(os.path.join(HERE, path), 'rb').read()
    add(name, raw, hc(raw), timed=True)

# Edge corpus.
add('empty', b'', b'\x00')
for n in list(range(1, 17)) + [269, 270, 271, 524, 525, 526]:
    hand('lit%d' % n, [], rb(n))
for off in (1, 2, 3, 4, 5, 7, 8):
    for ml in (4, 5, 7, 18, 19, 20, 33, 273, 274, 275, 528, 529, 1000):
        tail = 5 if ml >= 7 else 12
        hand('o%d_m%d' % (off, ml), [(rb(off), off, ml)], rb(tail))
# Literal-length extension on a sequence that has a match.
for ln in (14, 15, 16, 269, 270, 271):
    hand('l%d_m19' % ln, [(rb(ln), 4, 19)], rb(5))
# Back-to-back matches with zero literals, mixed offsets.
seqs = [(rb(16), 16, 32)] + [(b'', o, m) for o, m in ((1, 4), (3, 19), (2, 274), (9, 15), (40, 255), (6, 8))]
hand('zero_lit_chain', seqs, rb(5))
# Farthest offset.
hand('o65535', [(rb(65535), 65535, 40)], rb(5))
hand('o65535_rle', [(rb(65536), 65535, 300), (b'', 1, 70)], rb(9))
# Latest legal match: ends exactly 5 bytes before the end of the block.
hand('late_match', [(rb(20), 20, 7)], rb(5))
# Incompressible.
inc = rb(4096)
add('incompressible', inc, lz4.block.compress(inc, store_size=False))
# Reference compressor across levels on mixed synthetic data.
text = open(os.path.join(HERE, 'data/cdromdrive-64k.md'), 'rb').read()
mixed = bytearray()
while len(mixed) < 24576:
    k = rng.randrange(4)
    if k == 0:
        mixed += rb(rng.randrange(1, 300))
    elif k == 1:
        mixed += bytes([rng.randrange(256)]) * rng.randrange(1, 600)
    elif k == 2:
        s = rng.randrange(len(text) - 1000)
        mixed += text[s:s + rng.randrange(1, 900)]
    else:
        pat = rb(rng.randrange(1, 9))
        mixed += pat * rng.randrange(1, 80)
mixed = bytes(mixed)
add('mixed_fast1', mixed, lz4.block.compress(mixed, store_size=False))
add('mixed_fast9', mixed, lz4.block.compress(mixed, mode='fast', acceleration=9, store_size=False))
for lvl in (1, 4, 9, 12):
    add('mixed_hc%d' % lvl, mixed, lz4.block.compress(mixed, mode='high_compression', compression=lvl, store_size=False))
for n in (1, 5, 12, 13, 14, 64, 255):
    d = rb(n)
    add('ref_small%d' % n, d, lz4.block.compress(d, store_size=False))

host = os.path.join(HERE, 'lz4host')
os.environ['ASAN_OPTIONS'] = 'detect_leaks=0:abort_on_error=0'
out = bytearray(struct.pack('<I', len(streams)))
with tempfile.TemporaryDirectory() as td:
    for name, raw, comp, timed in streams:
        rp, cp = os.path.join(td, 'r'), os.path.join(td, 'c')
        open(rp, 'wb').write(raw)
        open(cp, 'wb').write(comp)
        chk = subprocess.run([host, 'check', rp, cp], capture_output=True, text=True)
        if chk.returncode != 0 or chk.stdout.strip() != 'OK':
            sys.exit('BASELINE FAILS %s: rc=%d %s %s' % (name, chk.returncode, chk.stdout, chk.stderr))
        margin = int(subprocess.run([host, 'margin', rp, cp], capture_output=True, text=True, check=True).stdout)
        print('%-20s raw %6d comp %6d margin %3d%s' % (name, len(raw), len(comp), margin, ' TIMED' if timed else ''))
        out += struct.pack('<24sIIII', name.encode(), 1 if timed else 0, len(raw), len(comp), margin)
        for blob in (raw, comp):
            out += blob
            while len(out) % 4:
                out.append(0)
open(os.path.join(HERE, 'corpus.bin'), 'wb').write(out)
print('streams %d, corpus.bin %d bytes' % (len(streams), len(out)))
