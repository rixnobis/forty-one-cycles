#!/usr/bin/env python3
"""Score generator for the megademo track: writes megademo.mml.

Everything is laid on a 16th-note grid. Each track is a list of (step, dur, note, vel);
the serializer turns that into the pinned MML dialect (monophonic per track)."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'megademo-cued.mml')

BARS = 120
NN = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}


def P(s):
    """'g#5' -> midi"""
    n = NN[s[0]]; i = 1
    while s[i] in '#b-':
        n += 1 if s[i] == '#' else -1; i += 1
    return n + 12 * (int(s[i:]) + 1)


# ---------------- harmony ----------------
CH = {  # name -> (root pc name, quality)
    'Am': ('a', 'min'), 'F': ('f', 'maj'), 'C': ('c', 'maj'), 'G': ('g', 'maj'), 'E': ('e', 'maj'),
    'Dm': ('d', 'min'), 'Em': ('e', 'min'), 'A': ('a', 'maj'), 'F#': ('f#', 'maj'),
}


def tones(ch, lo):
    r, q = CH[ch]; pc = P(r + '0') % 12
    root = lo + ((pc - lo) % 12)
    return [root, root + (3 if q == 'min' else 4), root + 7, root + 12]


P1 = ['Am', 'F', 'C', 'G']
P2 = ['Am', 'F', 'G', 'E']
BRK = ['Dm', 'Am', 'F', 'E']
chords = (P1 * 2                      # 1-8 intro
          + P1 * 2                    # 9-16 intro b
          + P1 * 2 + P1 + P2          # 17-32 groove
          + (P1 + P2) * 2             # 33-48 lead A
          + (P1 + P2) * 2             # 49-64 lead B
          + BRK * 2                   # 65-72 breakdown
          + ['Dm', 'F', 'G', 'E']     # 73-76
          + ['Em', 'G', 'A', 'F#']    # 77-80 build, F# = V of B minor
          + (P1 + P2) * 2             # 81-96 climax (k+2 -> B minor)
          + (P1 + P2) * 2             # 97-112 climax 2
          + P1 + ['Am', 'F', 'E', 'Am'])  # 113-120 outro
assert len(chords) == BARS
SHIFT_FROM = 81   # bars >= this are written in A minor and played +2 by the k token


def bar_of(b): return (b - 1) * 16


tracks = {}


def T(name): return tracks.setdefault(name, [])


def add(name, step, dur, note, vel):
    T(name).append((step, dur, note, vel))


def melody(name, bar, spec, vel=100, accent=None):
    """spec: 'e6:3 c6:3 r:2 ...' durations in 16ths, laid from the start of `bar`."""
    s = bar_of(bar)
    for i, tok in enumerate(spec.split()):
        n, d = tok.split(':'); d = int(d)
        if n != 'r':
            v = vel if accent is None else accent(i, vel)
            add(name, s, d, P(n), v)
        s += d


def section(bars): return range(bars[0], bars[1] + 1)


# ---------------- arps (cycle of six over chord tones: polymetric against the 4/4) ----------------
ARP_CYC = [0, 1, 2, 3, 2, 1]


def arp(bar, vel, name='arp', lo=64, cyc=ARP_CYC):
    t = tones(chords[bar - 1], lo)
    for k in range(16):
        acc = 14 if k % 4 == 0 else (6 if k % 2 == 0 else 0)
        add(name, bar_of(bar) + k, 1, t[cyc[(bar_of(bar) + k) % len(cyc)]] + 0, min(127, vel + acc))


# ---------------- bass ----------------
ROLL = [0, 0, 12, 0, 0, 12, 0, 0, 0, 0, 12, 0, 0, 12, 0, 7]
RVEL = [118, 70, 96, 72, 104, 92, 74, 70, 112, 70, 96, 72, 104, 92, 80, 96]


def bass(bar, style, vel=100):
    r = tones(chords[bar - 1], 33)[0]
    s = bar_of(bar)
    if style == 'off8':
        for b in range(4): add('bass', s + 4 * b + 2, 2, r + (12 if b % 2 else 0), vel)
    elif style == 'roll':
        for k in range(16): add('bass', s + k, 1, r + ROLL[k], RVEL[k] * vel // 100)
    elif style == 'pulse':
        for b in range(4): add('bass', s + 4 * b, 3, r, vel if b % 2 == 0 else vel - 20)


def sub(bar, vel=110):
    r = tones(chords[bar - 1], 28)[0]
    add('sub', bar_of(bar), 8, r, vel); add('sub', bar_of(bar) + 8, 6, r, vel - 15)


# ---------------- stabs, pad, hits ----------------
STAB_RHY = [0, 3, 6, 10, 12]


def stab(bar, vel, rhy=STAB_RHY):
    ch = chords[bar - 1]; q = CH[ch][1]
    root = tones(ch, 62)[0]
    for k in rhy:
        add('stabmin' if q == 'min' else 'stabmaj', bar_of(bar) + k, 2, root, vel - (0 if k in (0, 6) else 14))


def pad(bar, vel=72, length=16):
    t = tones(chords[bar - 1], 57)
    for i, n in enumerate(t[:3]): add(f'pad{i}', bar_of(bar), length, n, vel)


def orch(bar, vel=110, step=0):
    add('orch', bar_of(bar) + step, 8, tones(chords[bar - 1], 45)[0], vel)


# ---------------- drums ----------------
KIT = {'kick': 36, 'rim': 37, 'snare': 38, 'clap': 39, 'snare2': 40, 'chh': 42, 'chh2': 44,
       'ohh': 46, 'crash': 49, 'ride': 51, 'tomL': 41, 'tomM': 45, 'tomH': 48}
VEL = {'X': 124, 'x': 104, 'o': 86, '-': 58, '.': 0}


def drum(track, bar, pat, sound=None, scale=100):
    sound = sound or track
    for k, c in enumerate(pat.replace(' ', '')):
        if c != '.':
            add('d_' + track, bar_of(bar) + k, 1, KIT[sound], VEL[c] * scale // 100)


def hit(track, bar, step, sound, vel):
    add('d_' + track, bar_of(bar) + step, 1, KIT[sound], vel)


def fill_snare(bar, start=8, vel0=70, vel1=124, sound='snare2'):
    n = 16 - start
    for i in range(n):
        hit('fill', bar, start + i, sound, vel0 + (vel1 - vel0) * i // max(1, n - 1))


def fill_toms(bar):
    for i, s in enumerate(['tomH', 'tomH', 'tomM', 'tomM', 'tomL', 'tomL', 'snare', 'snare']):
        hit('fill', bar, 8 + i, s, 100 + i * 3)


KICK4 = 'x... x... x... x...'
CLAP = '.... x... .... x...'
HAT16 = '-o-o -o-o -o-o -o-o'
HATOFF = '..o. ..o. ..o. ..o.'
OHH = '..o. ..o. ..o. ..o.'


def groove(bar, full=True, hats='16'):
    drum('kick', bar, KICK4)
    if full:
        drum('clap', bar, CLAP); drum('snare', bar, CLAP.replace('x', 'o'), 'snare', 70)
    drum('hat', bar, HAT16 if hats == '16' else HATOFF, 'chh')
    if hats == '16' and full: drum('ohh', bar, OHH, 'ohh', 80)


def fx(bar, step, what, vel=110):
    add('fx', bar_of(bar) + step, 16, {'yeah': 60, 'hey': 62, 'noise': 64, 'laser': 65, 'zap': 67}[what], vel)


def rev(bar, vel=100):
    # reversed hit peaks 1.255 s after its start: 2.93 beats at 140 bpm -> start 12 steps early
    add('rev', bar_of(bar) - 12, 12, 60, vel)


# ======================= the arrangement =======================
# ---- 1-8 intro: strings + rising arp, roll into bar 9
for b in section((1, 8)):
    pad(b, 60 + 3 * (b - 1))
    arp(b, 34 + 7 * (b - 1))
fx(7, 0, 'noise', 70); fx(8, 0, 'noise', 95)
fill_snare(8, 0, 30, 115)
rev(9)

# ---- 9-16 intro b: four on the floor, offbeat bass
for b in section((9, 16)):
    pad(b, 70); arp(b, 86)
    drum('kick', b, KICK4); drum('hat', b, HATOFF, 'chh')
    bass(b, 'off8', 96)
    if b >= 13: drum('hat2', b, '-.-. -.-. -.-. -.-.', 'chh2')
hit('cym', 9, 0, 'crash', 115); fx(9, 0, 'yeah', 120); fx(13, 0, 'hey', 110)
fill_snare(16, 8, 60, 120)
rev(17)

# ---- 17-32 groove: rolling bass, stabs, full kit
for b in section((17, 32)):
    groove(b); bass(b, 'roll'); stab(b, 112); arp(b, 82)
    if b >= 25: pad(b, 58)
for b in (17, 25): hit('cym', b, 0, 'crash', 120)
fill_snare(24, 12, 80, 118); fill_toms(32)
fx(29, 0, 'zap', 90)

# ---- 33-48 lead A
LEAD_A = [
    'e6:3 c6:3 a5:2 e6:3 c6:3 b5:2',
    'c6:3 a5:3 f5:2 c6:3 a5:3 g5:2',
    'e6:3 c6:3 g5:2 e6:3 d6:3 c6:2',
    'd6:3 b5:3 g5:2 d6:4 b5:2 d6:2',
    'c6:3 b5:3 a5:2 e6:8',
    'f6:3 e6:3 c6:2 a5:8',
    'b5:3 c6:3 d6:2 g6:4 f6:2 e6:2',
    'e6:4 d6:2 c6:2 b5:4 g#5:4',
]
LEAD_B = [
    'a5:1 b5:1 c6:1 e6:1 a6:4 e6:2 g6:2 e6:2 c6:2',
    'f6:4 e6:2 c6:2 a5:2 c6:2 f6:2 a6:2',
    'g6:6 e6:2 c6:2 e6:2 g6:2 c7:2',
    'b6:4 d7:2 b6:2 g6:4 d6:4',
    'c7:4 a6:2 e6:2 c6:4 e6:2 a6:2',
    'a6:4 f6:2 c6:2 a5:4 c6:2 f6:2',
    'd6:2 g6:2 b6:2 d7:2 b6:2 d7:2 g6:2 b6:2',
    'g#6:4 e6:2 b5:2 g#6:4 b6:4',
]
COUNTER_B = ['e5:16', 'f5:8 a5:8', 'g5:16', 'g5:8 b5:8', 'a5:8 c6:8', 'a5:16', 'b5:8 d6:8', 'b5:8 g#5:8']

# scale-aware third below, for doubling the lead
SCALE = [9, 11, 0, 2, 4, 5, 7]   # A natural minor pitch classes


def third_below(n, chord):
    pcs = SCALE if chord != 'E' else [9, 11, 0, 2, 4, 5, 8]
    pc = n % 12
    if pc not in pcs: return n - 3
    i = pcs.index(pc); t = pcs[(i - 2) % 7]
    d = (pc - t) % 12
    return n - d


def harmonize(src, dst, bar0, nbars, vel):
    s0, s1 = bar_of(bar0), bar_of(bar0 + nbars)
    for (s, d, n, v) in [e for e in T(src) if s0 <= e[0] < s1]:
        add(dst, s, d, third_below(n, chords[s // 16]), vel)


def acc_lead(i, v): return v


for half in (0, 1):
    b0 = 33 + 8 * half
    for i, m in enumerate(LEAD_A): melody('lead', b0 + i, m, 104)
for b in section((33, 48)):
    groove(b); bass(b, 'roll', 92); arp(b, 70)
    stab(b, 96, [0, 6]); pad(b, 56)
harmonize('lead', 'counter', 41, 8, 84)
for b in (33, 41): hit('cym', b, 0, 'crash', 118)
fill_snare(40, 12, 80, 118); fill_toms(48)

# ---- 49-64 lead B + counter + orch
for half in (0, 1):
    b0 = 49 + 8 * half
    for i, m in enumerate(LEAD_B): melody('lead', b0 + i, m, 108)
    for i, m in enumerate(COUNTER_B): melody('counter', b0 + i, m, 80)
for b in section((49, 64)):
    groove(b); bass(b, 'roll'); arp(b, 74); stab(b, 104)
    if b % 2 == 1: orch(b, 112)
for b in (49, 57): hit('cym', b, 0, 'crash', 120)
fx(49, 0, 'yeah', 118); fill_snare(56, 12, 80, 118)
fill_snare(64, 0, 40, 124)
rev(65, 90)

# ---- 65-80 breakdown and build
BRK_MEL = ['d6:12 e6:4', 'c6:16', 'a5:8 c6:4 f6:4', 'e6:16',
           'd6:12 e6:4', 'f6:8 e6:4 c6:4', 'd6:8 c6:4 a5:4', 'b5:8 g#5:8']
for i, m in enumerate(BRK_MEL): melody('counter', 65 + i, m, 96)
for b in section((65, 80)):
    pad(b, 80); arp(b, 46 if b < 73 else 46 + 6 * (b - 72)); sub(b, 112)
hit('cym', 65, 0, 'crash', 100); orch(65, 100)
melody('lead', 73, 'd6:3 a5:3 f5:2 d6:3 a5:3 e6:2', 96)
melody('lead', 74, 'f6:3 c6:3 a5:2 f6:3 c6:3 g6:2', 100)
melody('lead', 75, 'g6:3 d6:3 b5:2 g6:3 d6:3 a6:2', 104)
melody('lead', 76, 'g#6:8 b6:8', 108)
melody('lead', 77, 'e6:16', 100)
melody('lead', 78, 'g6:16', 104)
melody('lead', 79, 'a6:8 c#7:8', 110)
melody('lead', 80, 'f#6:8 a#6:4 c#7:4', 116)
for b in section((73, 76)): drum('hat', b, HATOFF, 'chh', 80)
for b in section((77, 79)):
    drum('kick', b, KICK4); drum('hat', b, HAT16, 'chh')
    for k in range(0, 16, 4 if b == 77 else 2): hit('fill', b, k, 'snare2', 60 + 4 * (b - 77) * 4 + k)
for k in range(16): hit('fill', 80, k, 'snare2', 70 + 3 * k)
drum('kick', 80, 'x... x... x... x...')
fx(77, 0, 'noise', 80); fx(79, 0, 'noise', 110)
rev(81, 112)

# ---- 81-96 climax (B minor): lead A with a third below on top of everything
for half in (0, 1):
    b0 = 81 + 8 * half
    for i, m in enumerate(LEAD_A): melody('lead', b0 + i, m, 110)
harmonize('lead', 'counter', 81, 16, 88)
for b in section((81, 96)):
    groove(b); bass(b, 'roll'); stab(b, 108); arp(b, 80); pad(b, 60)
    if b % 4 == 1: orch(b, 116)
for b in (81, 89): hit('cym', b, 0, 'crash', 124)
fx(81, 0, 'yeah', 124); fill_snare(88, 12, 80, 118); fill_toms(96)

# ---- 97-112 climax 2: lead B, counter, orch every other bar
for half in (0, 1):
    b0 = 97 + 8 * half
    for i, m in enumerate(LEAD_B): melody('lead', b0 + i, m, 112)
    for i, m in enumerate(COUNTER_B): melody('counter', b0 + i, m, 84)
for b in section((97, 112)):
    groove(b); bass(b, 'roll'); stab(b, 108); arp(b, 82); pad(b, 56)
    if b % 2 == 1: orch(b, 114)
for b in (97, 105): hit('cym', b, 0, 'crash', 124)
fx(105, 0, 'hey', 116); fill_snare(104, 12, 80, 120); fill_snare(112, 8, 70, 124)

# ---- 113-120 outro: strip down and stop
for b in section((113, 118)):
    fade = 100 - 9 * (b - 113)
    drum('kick', b, KICK4, scale=fade); drum('hat', b, HATOFF, 'chh', fade)
    if b <= 116: drum('clap', b, CLAP, scale=fade)
    bass(b, 'off8', fade); arp(b, 40 + fade // 2); pad(b, 50)
melody('counter', 113, 'e6:16', 70); melody('counter', 114, 'c6:16', 64)
melody('counter', 115, 'g5:8 e5:8', 60); melody('counter', 116, 'g5:8 b5:8', 56)
hit('cym', 113, 0, 'crash', 110)
rev(119, 100)
orch(119, 124); hit('cym', 119, 0, 'crash', 124); drum('kick', 119, 'X...............')
add('pad0', bar_of(119), 16, tones('Am', 57)[0], 70); add('pad1', bar_of(119), 16, tones('Am', 57)[1], 70)
add('pad2', bar_of(119), 16, tones('Am', 57)[2], 70)
# inaudible note (velocity 1 -> 0 through the player's quadratic curve) so the song's END lands
# two bars after the final hit and the reverb tail decays before the player loops to the top
add('fx', bar_of(121) + 15, 1, 65, 1)

# ---- arp echo: three 16ths late, softer, panned opposite (the tracker echo trick)
for (s, d, n, v) in list(T('arp')):
    if s + 3 < bar_of(BARS + 1): add('arpecho', s + 3, d, n, max(1, v * 50 // 100))

# ======================= serializer =======================
LEN = {1: '16', 2: '8', 3: '8.', 4: '4', 6: '4.', 8: '2', 12: '2.', 16: '1'}
NAMES = ['c', 'c+', 'd', 'd+', 'e', 'f', 'f+', 'g', 'g+', 'a', 'a+', 'b']


def rests(n):
    out = []
    for L, tok in ((16, '1'), (8, '2'), (4, '4'), (2, '8'), (1, '16')):
        while n >= L: out.append('r' + tok); n -= L
    return out


def ser(evs, drums=False, shift_at=None):
    evs = sorted(evs)
    out, t, curv, shifted = [], 0, None, False
    for i, (s, d, n, v) in enumerate(evs):
        if s < t: continue                       # monophonic: drop a collision
        nxt = evs[i + 1][0] if i + 1 < len(evs) else 1 << 30
        d = min(d, nxt - s)
        if shift_at is not None and not shifted and s >= shift_at:
            if t < shift_at: out += rests(shift_at - t); t = shift_at
            out.append('k2'); shifted = True
        out += rests(s - t)
        if v != curv: out.append(f'v{v}'); curv = v
        dd = max(L for L in LEN if L <= d)
        if drums:
            out.append(f'l{LEN[dd]} n{n}')
        else:
            out.append(f'o{n // 12 - 1}{NAMES[n % 12]}{LEN[dd]}')
        out += rests(d - dd)
        t = s + d
    return ' '.join(out)


SETUP = [  # name, ch, prog, opts
    ('lead', 1, 2, 'rev=40 pan=64 vol=108 gate=96'),
    ('counter', 2, 3, 'rev=55 pan=84 vol=86 gate=96'),
    # psmplayer's CC91 is binary (>0 = full send into the fixed hall), so dry is the default
    ('arp', 3, 5, 'rev=0 pan=40 vol=82 gate=55'),
    ('arpecho', 4, 5, 'rev=60 pan=92 vol=82 gate=55'),
    ('bass', 5, 0, 'rev=0 pan=64 vol=112 gate=85'),
    ('sub', 6, 1, 'rev=0 pan=64 vol=110 gate=100'),
    ('stabmin', 7, 7, 'rev=0 pan=56 vol=96 gate=100'),
    ('stabmaj', 8, 8, 'rev=0 pan=56 vol=96 gate=100'),
    ('pad0', 9, 6, 'rev=80 pan=44 vol=84 gate=100'),
    ('pad1', 11, 6, 'rev=80 pan=64 vol=84 gate=100'),
    ('pad2', 12, 6, 'rev=80 pan=84 vol=84 gate=100'),
    ('orch', 13, 10, 'rev=55 pan=64 vol=104 gate=100'),
    ('rev', 14, 11, 'rev=40 pan=64 vol=96 gate=100'),
    ('fx', 15, 13, 'rev=50 pan=64 vol=100 gate=100'),
]
DR = ['kick', 'clap', 'snare', 'hat', 'hat2', 'ohh', 'cym', 'fill']
NOSHIFT = {'fx', 'rev'}

# Mix trim, linear on CC7 (psmplayer multiplies vel*vol*expr*tonevol). At 1.0 the Redux
# capture clipped 4537 samples (sections at -12.5 dB RMS); 0.55 = -5.2 dB.
VOL_SCALE = 0.55


def trim(opts):
    import re
    return re.sub(r'vol=(\d+)', lambda m: f'vol={round(int(m.group(1)) * VOL_SCALE)}', opts)


with open(OUT, 'w') as f:
    f.write('; megademo - 140 bpm, A minor, lifts to B minor at bar 81. GENERATED by tools/compose.py\n')
    f.write('#tempo 140\n#ppq 48\n\n')
    # Cue track first, so a cue sorts ahead of the notes that share its tick.
    # Tag 1 = BAR (payload = bar number 1..121), tag 2 = SECTION (payload = section index 0..8).
    SECTION_BARS = [1, 9, 17, 33, 49, 65, 81, 97, 113]
    cue = []
    for bar in range(1, 122):
        cue.append(f'!1,{bar}')
        if bar in SECTION_BARS: cue.append(f'!2,{SECTION_BARS.index(bar)}')
        cue.append('r1')
    f.write('@track cues ch=16\n' + ' '.join(cue) + '\n\n')
    for name, ch, prog, opts in SETUP:
        if not T(name): continue
        f.write(f'@track {name} ch={ch} prog={prog} v=100 {trim(opts)}\n')
        f.write(ser(T(name), shift_at=None if name in NOSHIFT else bar_of(SHIFT_FROM)) + '\n\n')
    for d in DR:
        if not T('d_' + d): continue
        # one MIDI channel for the whole kit, so its controllers are set once, on the kick track
        opts = 'rev=0 pan=64 vol=112 ' if d == 'kick' else ''
        f.write(f'@track d_{d} ch=10 v=100 {trim(opts)}gate=100\n')
        f.write(ser(T('d_' + d), drums=True) + '\n\n')
print('wrote', OUT, {k: len(v) for k, v in tracks.items()})
