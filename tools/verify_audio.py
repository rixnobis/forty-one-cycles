#!/usr/bin/env python3
"""Check that the cued MIDI plays the same as the uncued one.

Usage: verify_audio.py base.mid cued.mid
Cue metas (FF 07) and the cue track are ignored; every other event of every other
track must match by absolute tick and bytes. Runs a negative control (one note moved)
that must fail."""
import struct, sys


def vlq(d, i):
    v = 0
    while True:
        b = d[i]; i += 1
        v = (v << 7) | (b & 0x7f)
        if not b & 0x80: return v, i


def parse(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'MThd'
    ntr, ppq = struct.unpack('>HH', d[10:14])[0:2]
    ntr = struct.unpack('>H', d[10:12])[0]
    i = 14; tracks = []
    for _ in range(ntr):
        assert d[i:i + 4] == b'MTrk'
        ln = struct.unpack('>I', d[i + 4:i + 8])[0]; j = i + 8; end = j + ln
        t = 0; run = None; evs = []; name = None; cues = 0
        while j < end:
            dt, j = vlq(d, j); t += dt
            st = d[j]
            if st == 0xff:
                typ = d[j + 1]; ln2, k = vlq(d, j + 2); body = d[k:k + ln2]; j = k + ln2
                if typ == 0x07: cues += 1; continue
                if typ == 0x03: name = body
                evs.append((t, bytes([0xff, typ]) + body))
            else:
                if st & 0x80: run = st; j += 1
                n = 1 if (run & 0xf0) in (0xc0, 0xd0) else 2
                evs.append((t, bytes([run]) + d[j:j + n])); j += n
        tracks.append((name, evs, cues)); i = end
    return ppq, tracks


def comparable(path):
    ppq, tr = parse(path)
    return ppq, {n: sorted(e) for n, e, c in tr if n != b'cues'}, sum(c for _, _, c in tr)


def compare(a, b):
    pa, ta, _ = a; pb, tb, _ = b
    if pa != pb or set(ta) != set(tb): return False, f'ppq/tracks differ {pa} {pb}'
    bad = [n for n in ta if ta[n] != tb[n]]
    return not bad, f'differing tracks: {bad}'


if __name__ == '__main__':
    base = comparable(sys.argv[1]); cued = comparable(sys.argv[2])
    n_ev = sum(len(v) for v in base[1].values())
    print(f'base: {len(base[1])} tracks, {n_ev} events, {base[2]} cues')
    print(f'cued: {len(cued[1])} tracks, {sum(len(v) for v in cued[1].values())} events, {cued[2]} cues')
    ok, why = compare(base, cued)
    print('MATCH' if ok else f'MISMATCH ({why})')
    # negative control: move one note-on of the lead by a tick; the comparison must notice
    mut = {n: list(v) for n, v in cued[1].items()}
    lead = mut[b'lead']
    k = next(i for i, (t, e) in enumerate(lead) if e[0] & 0xf0 == 0x90)
    lead[k] = (lead[k][0] + 1, lead[k][1])
    nok, nwhy = compare(base, (cued[0], {n: sorted(v) for n, v in mut.items()}, 0))
    print('negative control:', 'FAILED AS REQUIRED (' + nwhy + ')' if not nok else 'BROKEN: mutated file still matched')
    sys.exit(0 if ok and not nok else 1)
