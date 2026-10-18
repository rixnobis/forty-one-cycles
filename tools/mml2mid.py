#!/usr/bin/env python3
"""Compile the pinned MML dialect in keynote.mml to a Standard MIDI File (format 1)."""
import random, re, struct, sys

PPQ = 480
WHOLE = 4 * PPQ
NOTE = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}


def parse_file(text):
    lines = [l.split(';', 1)[0].rstrip() for l in text.splitlines()]
    hdr, macros, tracks, cur = {}, {}, [], None
    for l in lines:
        if not l.strip():
            continue
        m = re.match(r'#(\w+)\s+(\S+)', l)
        if m:
            hdr[m.group(1)] = m.group(2); continue
        m = re.match(r'\$(\w+)\s*=\s*(.*)', l)
        if m:
            macros[m.group(1)] = m.group(2); continue
        m = re.match(r'@track\s+(\w+)(.*)', l)
        if m:
            opts = dict(kv.split('=') for kv in m.group(2).split())
            cur = {'name': m.group(1), 'opts': opts, 'body': []}
            tracks.append(cur); continue
        cur['body'].append(l)
    return hdr, macros, tracks


def expand(s, macros, depth=0):
    if depth > 20:
        raise ValueError('macro recursion')
    out = re.sub(r'\$(\w+)', lambda m: ' ' + macros[m.group(1)] + ' ', s)
    return out if out == s else expand(out, macros, depth + 1)


def length(tok_num, dots, default):
    t = WHOLE // int(tok_num) if tok_num else default
    add, base = 0, t
    for _ in dots:
        base //= 2; add += base
    return t + add


def compile_track(body, transpose, drums, vel0):
    src = body
    pos = 0
    ev = []  # (tick, dur, note, vel) or ('prog', tick, p)
    st = {'t': 0, 'o': 4, 'l': WHOLE // 4, 'v': vel0, 'k': 0}

    def num():
        nonlocal pos
        m = re.match(r'\d+', src[pos:])
        if not m: return None
        pos += m.end(); return m.group(0)

    def dots():
        nonlocal pos
        m = re.match(r'\.*', src[pos:]); pos += m.end(); return m.group(0)

    def pitch(letter, octv):
        nonlocal pos
        p = NOTE[letter]
        while pos < len(src) and src[pos] in '+#-':
            p += -1 if src[pos] == '-' else 1; pos += 1
        return 12 * (octv + 1) + p

    def run(end=None):
        nonlocal pos
        while pos < len(src):
            c = src[pos]; pos += 1
            if c.isspace(): continue
            if c == end: return
            if c == 'o': st['o'] = int(num())
            elif c == 'l':
                n = num(); st['l'] = length(n, dots(), None)
            elif c == 'v': st['v'] = int(num())
            elif c == 'k':   # megademo copy: key shift from here on, kN or k-N
                neg = src[pos] == '-'
                if neg: pos += 1
                st['k'] = -int(num()) if neg else int(num())
            elif c == 'y':   # megademo copy: controller change yCC,VAL
                cc = int(num()); assert src[pos] == ','; pos += 1
                ev.append(('cc', st['t'], cc, int(num())))
            elif c == '>': st['o'] += 1
            elif c == '<': st['o'] -= 1
            elif c == '@': ev.append(('prog', st['t'], int(num())))
            elif c == '!':
                # user cue: !tag,value -> MIDI Cue Point meta event with text "tag:value"
                tag = int(num())
                assert src[pos] == ',', f'cue needs !tag,value at {pos}'
                pos += 1
                ev.append(('cue', st['t'], tag, int(num())))
            elif c == '[':
                start = pos
                depth = 1
                while depth:
                    depth += {'[': 1, ']': -1}.get(src[pos], 0); pos += 1
                inner_end = pos - 1
                reps = int(num() or 2)
                after = pos
                for _ in range(reps):
                    pos = start; run(']')
                pos = after
            elif c == '{':
                o_save, notes = st['o'], []
                while src[pos] != '}':
                    d = src[pos]; pos += 1
                    if d in NOTE: notes.append(pitch(d, st['o']))
                    elif d == '>': st['o'] += 1
                    elif d == '<': st['o'] -= 1
                pos += 1
                st['o'] = o_save
                dur = length(num(), dots(), st['l'])
                for n in notes: ev.append((st['t'], dur, n + transpose + (0 if drums else st['k']), st['v']))
                st['t'] += dur
            elif c in NOTE:
                n = pitch(c, st['o']); dur = length(num(), dots(), st['l'])
                ev.append((st['t'], dur, n + transpose + (0 if drums else st['k']), st['v'])); st['t'] += dur
            elif c == 'n':
                n = int(num()); dur = st['l']
                ev.append((st['t'], dur, n, st['v'])); st['t'] += dur
            elif c == 'r':
                st['t'] += length(num(), dots(), st['l'])
            else:
                raise SyntaxError(f'unexpected {c!r} at {pos}: ...{src[pos-10:pos+10]}...')
    run()
    return ev, st['t']


def vlq(n):
    b = [n & 0x7f]; n >>= 7
    while n: b.insert(0, (n & 0x7f) | 0x80); n >>= 7
    return bytes(b)


def chunk(events):
    events.sort(key=lambda e: (e[0], e[1]))
    out, last = b'', 0
    for t, _, data in events:
        out += vlq(t - last) + data; last = t
    out += b'\x00\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(out)) + out


def build(path):
    hdr, macros, tracks = parse_file(open(path).read())
    # ticks per quarter note; lower it for a PS1 player, which polls once per tick
    global PPQ, WHOLE
    PPQ = int(hdr.get('ppq', 480))
    WHOLE = 4 * PPQ
    tempo = int(hdr.get('tempo', 120)); tr = int(hdr.get('transpose', 0))
    t0 = [(0, 0, b'\xff\x51\x03' + struct.pack('>I', 60000000 // tempo)[1:]),
          (0, 0, b'\xff\x58\x04\x04\x02\x18\x08')]
    chunks, notes_out = [chunk(t0)], []
    for trk in tracks:
        o = trk['opts']; ch = int(o['ch']) - 1; drums = ch == 9
        body = expand(' '.join(trk['body']), macros)
        ev, end = compile_track(body, 0 if drums else tr, drums, int(o.get('v', 100)))
        print(f"{trk['name']:8s} {end / WHOLE:6.2f} bars  {sum(1 for e in ev if e[0] not in ('prog', 'cue', 'cc'))} notes"
              f"  {sum(1 for e in ev if e[0] == 'cue')} cues")
        mev = [(0, 0, bytes([0x21, 0xff, 0x03, 0x01]) + b'\x00')] if False else []
        mev.append((0, 0, b'\xff\x03' + vlq(len(trk['name'])) + trk['name'].encode()))
        if 'prog' in o: mev.append((0, 1, bytes([0xc0 | ch, int(o['prog'])])))
        # track options -> controllers: rev (CC91), cho (CC93), pan (CC10, 0-127), vol (CC7)
        for key, cc in (('rev', 91), ('cho', 93), ('pan', 10), ('vol', 7)):
            if key in o: mev.append((0, 1, bytes([0xb0 | ch, cc, int(o[key])])))
        hum = int(hdr.get('humanize', 0))
        rng = random.Random(trk['name'])
        for e in ev:
            if e[0] == 'prog':
                mev.append((e[1], 1, bytes([0xc0 | ch, e[2]]))); continue
            if e[0] == 'cc':
                mev.append((e[1], 1, bytes([0xb0 | ch, e[2], e[3]]))); continue
            if e[0] == 'cue':
                txt = f'{e[2]}:{e[3]}'.encode()
                mev.append((e[1], 1, b'\xff\x07' + vlq(len(txt)) + txt)); continue
            t, dur, n, v = e
            if hum and not drums:
                # small, seeded (so the file is reproducible) timing and velocity drift
                t = max(0, t + rng.randint(-PPQ // 48, PPQ // 48))
                v = min(127, max(1, v + rng.randint(-hum, hum)))
            if 'gate' in o:   # percent of the written length; megademo copy addition
                gate = max(1, dur * int(o['gate']) // 100)
            else:
                gate = PPQ // 8 if drums else max(dur - PPQ // 16, dur * 9 // 10)
            mev.append((t, 2, bytes([0x90 | ch, n, v])))
            mev.append((t + gate, 0, bytes([0x80 | ch, n, 0])))
            notes_out.append((trk['name'], t, dur, n))
        chunks.append(chunk(mev))
    data = b'MThd' + struct.pack('>IHHH', 6, 1, len(chunks), PPQ) + b''.join(chunks)
    return data, notes_out, tempo


if __name__ == '__main__':
    data, _, _ = build(sys.argv[1])
    open(sys.argv[2], 'wb').write(data)
