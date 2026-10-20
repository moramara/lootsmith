#!/usr/bin/env python3
"""
LOOT SMITH - audio asset generator.

Every sound of the game is synthesised from scratch by this script (additive / subtractive synthesis, no samples,
no recordings, no third-party material), so there is nothing to attribute.  Re-running it rewrites
assets/audio/bgm_hearth.ogg and assets/audio/se/*.wav byte-for-byte reproducibly (fixed random seeds).

    python3 tools/gen_audio.py            # needs numpy + scipy; the BGM is encoded with ffmpeg (libvorbis)
    python3 tools/gen_audio.py --check    # only print the measurements (peak / RMS / loop seam), write nothing

BGM  96 BPM, 4/4, 32 bars = exactly 80.000 s = 3 528 000 samples at 44.1 kHz.  Every note and the reverb are rendered
     *circularly* (tails wrap around the end of the loop, reverb = circular FFT convolution), so the last sample is
     followed seamlessly by the first one: no click, no silence, no level jump at the seam.
SE   mono 16 bit 44.1 kHz WAV, 1 ms fade-in / short fade-out so every file starts and ends at zero.
"""
import os, sys, wave, subprocess, tempfile
import numpy as np
from scipy import signal

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "audio")
CHECK_ONLY = "--check" in sys.argv

# ----------------------------------------------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------------------------------------------
NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    n = NOTE[name[0]]
    i = 1
    if name[i] == "#":
        n += 1; i += 1
    elif name[i] == "b":
        n -= 1; i += 1
    return 12 * (int(name[i:]) + 1) + n


def hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def tarr(dur):
    return np.arange(int(round(dur * SR))) / SR


def lp(x, fc, order=2):
    return signal.sosfilt(signal.butter(order, fc, "low", fs=SR, output="sos"), x)


def hp(x, fc, order=2):
    return signal.sosfilt(signal.butter(order, fc, "high", fs=SR, output="sos"), x)


def bp(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, hi], "band", fs=SR, output="sos"), x)


def fade(x, fin=0.001, fout=0.01):
    x = x.copy()
    a, b = int(fin * SR), int(fout * SR)
    if a > 0:
        x[:a] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(a) / a)
    if b > 0:
        x[-b:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(b) / b)
    return x


def db(x):
    return 20 * np.log10(max(float(x), 1e-12))


class Track:
    """a growing mono buffer: add(signal, start_seconds, gain)"""

    def __init__(self):
        self.x = np.zeros(0)

    def add(self, sig, t0=0.0, g=1.0):
        s = int(round(t0 * SR))
        e = s + len(sig)
        if e > len(self.x):
            self.x = np.concatenate([self.x, np.zeros(e - len(self.x))])
        self.x[s:e] += sig * g
        return self


# ---- instruments (mono, float) -----------------------------------------------------------------------------------
def pluck(f, dur, bright=1.0, decay=2.6, seed=0):
    """lute / harp like plucked string: decaying harmonics (higher ones die faster) + a tiny pick transient"""
    r = np.random.default_rng(seed + int(f))
    t = tarr(dur)
    out = np.zeros_like(t)
    k = 1
    while k <= 14 and f * k < 9000:
        fk = f * k * np.sqrt(1 + 1.2e-4 * k * k)                 # slight string stiffness
        a = (1.0 / k ** 1.25) * (bright ** (k - 1))
        out += a * np.sin(2 * np.pi * fk * t + r.uniform(0, 0.6)) * np.exp(-t * (decay + 0.85 * (k - 1)))
        k += 1
    n = bp(r.standard_normal(len(t)), 1500, 5000) * np.exp(-t * 380) * 0.06
    out += n
    return fade(out, 0.002, min(0.05, dur / 3))


def flute(f, dur, vel=1.0, seed=0):
    """soft wooden flute / recorder: nearly sine, delayed vibrato, a breath of noise"""
    r = np.random.default_rng(seed + int(f * 3))
    n = int(round((dur + 0.07) * SR))
    t = np.arange(n) / SR
    vib = 1 + 0.0042 * np.sin(2 * np.pi * 5.2 * t) * np.clip((t - 0.16) / 0.25, 0, 1)
    ph = 2 * np.pi * np.cumsum(f * vib) / SR
    sig = np.sin(ph) + 0.22 * np.sin(2 * ph) + 0.07 * np.sin(3 * ph) + 0.025 * np.sin(4 * ph)
    a, rel = 0.045, 0.07
    env = np.minimum(1, t / a) ** 1.5
    env *= np.where(t > dur, np.exp(-(t - dur) / (rel / 4)), 1.0)
    env *= 0.86 + 0.14 * np.sin(np.pi * np.clip(t / max(dur, 0.1), 0, 1))     # tiny swell
    breath = bp(r.standard_normal(n), 1800, 5200) * (0.05 + 0.12 * np.exp(-t * 14))
    return fade((sig * 0.55 + breath * 0.5) * env * vel, 0.002, 0.02)


def pad_note(f, dur, seed=0):
    """warm string pad: three slightly detuned band-limited saws, low-passed, slow attack / release"""
    r = np.random.default_rng(seed + int(f))
    n = int(round(dur * SR))
    t = np.arange(n) / SR
    out = np.zeros(n)
    for det in (-6.0, 0.0, 6.0):
        fd = f * 2 ** (det / 1200.0)
        ph0 = r.uniform(0, 6.28)
        for k in range(1, 9):
            out += (1.0 / k) * np.sin(2 * np.pi * fd * k * t + ph0 * k) / 3
    out = lp(out, 1100)
    a, rl = 0.55, 0.7
    env = np.minimum(1, t / a) * np.minimum(1, (dur - t) / rl)
    return out * np.clip(env, 0, 1) ** 1.3


def bass_note(f, dur):
    t = tarr(dur)
    s = np.sin(2 * np.pi * f * t) + 0.38 * np.sin(2 * np.pi * 2 * f * t) * np.exp(-t * 3) + 0.10 * np.sin(2 * np.pi * 3 * f * t) * np.exp(-t * 6)
    env = np.minimum(1, t / 0.006) * np.exp(-t * 2.1)
    return fade(lp(s * env, 700), 0.003, min(0.06, dur / 3))


def bell(f, dur, ratios=(1, 2.76, 5.40, 8.93), amps=(1, .5, .28, .14), decays=(3.0, 4.6, 7.0, 10.0)):
    t = tarr(dur)
    out = np.zeros_like(t)
    for r_, a_, d_ in zip(ratios, amps, decays):
        if f * r_ < 14000:
            out += a_ * np.sin(2 * np.pi * f * r_ * t) * np.exp(-t * d_)
    return fade(out, 0.0008, min(0.08, dur / 3))


def horn(f, dur, vel=1.0):
    """soft fanfare horn: harmonic stack, low-passed, gentle attack"""
    n = int(round((dur + 0.12) * SR))
    t = np.arange(n) / SR
    vib = 1 + 0.003 * np.sin(2 * np.pi * 5.0 * t) * np.clip((t - 0.1) / 0.2, 0, 1)
    ph = 2 * np.pi * np.cumsum(f * vib) / SR
    sig = sum((1.0 / k ** 1.15) * np.sin(k * ph) for k in range(1, 11))
    sig = lp(sig, 2300)
    env = np.minimum(1, t / 0.035) ** 1.2 * np.where(t > dur, np.exp(-(t - dur) / 0.03), 1.0)
    return fade(sig * env * vel * 0.5, 0.002, 0.03)


def room(x, wet=0.2, rt=0.5, seed=1, pre=0.008):
    """small reverb: convolution with decaying, low-passed noise"""
    r = np.random.default_rng(seed)
    n = int(rt * 1.2 * SR)
    ir = r.standard_normal(n) * np.exp(-np.arange(n) / SR * 6.9 / rt)
    ir = lp(ir, 5000)
    ir[: int(pre * SR)] = 0
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
    wetsig = signal.fftconvolve(x, ir)
    y = np.concatenate([x, np.zeros(len(wetsig) - len(x))]) * (1 - wet * 0.5) + wetsig * wet
    return y


def finish(x, peak, tail_fade=0.04, floor_db=-50.0):
    x = hp(x, 25, 1)
    x = x / (np.max(np.abs(x)) + 1e-12) * peak
    loud = np.nonzero(np.abs(x) > 10 ** (floor_db / 20))[0]          # drop the (almost) inaudible reverb tail
    if len(loud):
        x = x[: min(len(x), loud[-1] + int(0.02 * SR))]
    return fade(x, 0.001, min(tail_fade, len(x) / SR / 2))


# ----------------------------------------------------------------------------------------------------------------
# sound effects
# ----------------------------------------------------------------------------------------------------------------
def se_ui_click():
    t = tarr(0.07)
    s = np.sin(2 * np.pi * 1850 * t) * np.exp(-t * 170) + 0.8 * np.sin(2 * np.pi * 760 * t) * np.exp(-t * 95)
    s += bp(np.random.default_rng(3).standard_normal(len(t)), 2000, 6000) * np.exp(-t * 700) * 0.25
    return s


def se_ui_nav():
    t = tarr(0.09)
    f = 520 * (1 + 0.5 * np.clip(t / 0.05, 0, 1) ** 2)             # little upward chirp: "page turns forward"
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(ph) * np.exp(-t * 55) + 0.35 * np.sin(2 * ph) * np.exp(-t * 90)
    s += bp(np.random.default_rng(4).standard_normal(len(t)), 1500, 5000) * np.exp(-t * 500) * 0.15
    return lp(s, 4500)


def se_ui_back():
    t = tarr(0.1)
    f = 780 * (1 - 0.35 * np.clip(t / 0.07, 0, 1))                 # downward chirp: "step back"
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(ph) * np.exp(-t * 48) + 0.3 * np.sin(2 * ph) * np.exp(-t * 80)
    return lp(s, 3500)


def anvil_hit(t0, tr, amp=1.0, shift=1.0, seed=0):
    r = np.random.default_rng(seed)
    t = tarr(0.9)
    out = np.zeros_like(t)
    for f, d, a in ((1180, 5.5, 1.0), (1960, 8.0, 0.65), (3130, 11.0, 0.45), (4720, 16.0, 0.28), (7150, 24.0, 0.15)):
        out += a * np.sin(2 * np.pi * f * shift * (1 + r.uniform(-0.004, 0.004)) * t + r.uniform(0, 6)) * np.exp(-t * d)
    thump = np.sin(2 * np.pi * (170 * np.exp(-t * 25) + 70) * t) * np.exp(-t * 38) * 1.4
    click = hp(r.standard_normal(len(t)), 2500) * np.exp(-t * 900) * 1.0
    tr.add(fade(out * 0.55 + thump + click, 0.0004, 0.05), t0, amp)


def se_craft(rare=False):
    tr = Track()
    anvil_hit(0.0, tr, 1.0, 1.0, 1)
    anvil_hit(0.23, tr, 0.72, 1.045, 2)
    r = np.random.default_rng(7)
    t = tarr(0.35)
    sizzle = bp(r.standard_normal(len(t)), 3500, 9000) * (np.minimum(1, t / 0.05) * np.exp(-t * 9))
    tr.add(sizzle, 0.42, 0.07)
    if rare:                                                    # masterwork / legendary: a rising shimmer on top
        for i, n in enumerate(("E6", "G#6", "B6", "E7")):
            tr.add(bell(hz(midi(n)), 0.9, (1, 2.0, 3.0), (1, .3, .12), (3.2, 5.0, 8.0)), 0.5 + i * 0.085, 0.16)
    y = room(tr.x, 0.14, 0.4, 11)
    return y


def se_coin():
    tr = Track()
    for t0, f, a in ((0.0, 1900, 1.0), (0.075, 2450, 0.85), (0.15, 2100, 0.55)):
        tr.add(bell(f, 0.28, (1, 1.52, 2.34, 3.97), (1, .55, .35, .2), (13, 18, 25, 35)), t0, a)
        tr.add(hp(np.random.default_rng(int(f)).standard_normal(int(0.012 * SR)), 3000) * np.hanning(int(0.012 * SR)), t0, 0.25 * a)
    tr.add(bell(780, 0.2, (1, 2.1), (1, .3), (28, 40)), 0.0, 0.35)       # body of the coin
    return room(tr.x, 0.1, 0.25, 12)


def se_sell():
    tr = Track()
    for t0, f, a in ((0.0, 2050, 0.9), (0.07, 2600, 0.7)):
        tr.add(bell(f, 0.22, (1, 1.52, 2.34, 3.97), (1, .55, .35, .2), (14, 19, 27, 36)), t0, a)
    for t0, n, a, d in ((0.12, "G5", 0.6, 1.0), (0.22, "C6", 0.8, 1.0)):
        tr.add(bell(hz(midi(n)), 0.55, (1, 2.0, 3.0, 4.2), (1, .32, .14, .06), (5.5, 8.0, 11.0, 14.0)), t0, a * d)
        tr.add(pluck(hz(midi(n)), 0.3, 0.8, 6.0), t0, 0.25)
    return room(tr.x, 0.16, 0.35, 13)


def se_error():
    tr = Track()
    for t0, f in ((0.0, 233.1), (0.095, 174.6)):                # two soft falling "wooden" tones, a minor third apart
        t = tarr(0.13)
        s = (np.sin(2 * np.pi * f * t) + 0.32 * np.sin(4 * np.pi * f * t) + 0.12 * np.sin(6 * np.pi * f * t)) * np.exp(-t * 20)
        s *= np.minimum(1, t / 0.006)
        tr.add(lp(s, 1400), t0, 1.0)
    return tr.x


def fanfare(notes_melody, chord_sets, total, ring_note=None, harp=True, bells=True, timp=True):
    """notes_melody: (name, t0, dur, vel); chord_sets: (t0, dur, [names]) soft horn / harp chords underneath"""
    tr = Track()
    for n, t0, d, v in notes_melody:
        f = hz(midi(n))
        tr.add(horn(f, d, v), t0, 0.9)
        tr.add(horn(f * 2, d, v * 0.35), t0, 0.35)
        if harp:
            tr.add(pluck(f * 2, min(1.2, d + 0.5), 0.9, 3.2), t0, 0.28 * v)
    for t0, d, ch in chord_sets:
        for i, n in enumerate(ch):
            f = hz(midi(n))
            tr.add(horn(f, d, 0.55), t0 + i * 0.012, 0.32)
            if harp:
                tr.add(pluck(f, min(1.6, d + 0.4), 0.85, 2.6), t0 + i * 0.03, 0.26)
    if timp:
        for t0, a in timp_hits:
            pass
    return tr


timp_hits = []


def timpani(tr, t0, f=98.0, amp=1.0):
    t = tarr(0.6)
    s = np.sin(2 * np.pi * (f * (1 + 0.25 * np.exp(-t * 30))) * t) * np.exp(-t * 7)
    s += lp(np.random.default_rng(int(t0 * 1000) + 5).standard_normal(len(t)), 600) * np.exp(-t * 60) * 0.5
    tr.add(fade(s, 0.002, 0.1), t0, 0.5 * amp)


def sparkle(tr, t0, names, step=0.07, amp=0.2, dur=0.9):
    for i, n in enumerate(names):
        tr.add(bell(hz(midi(n)), dur, (1, 2.0, 3.0), (1, .3, .12), (3.4, 5.0, 8.0)), t0 + i * step, amp)


def se_contract():
    mel = [("C5", 0.00, 0.11, 0.9), ("E5", 0.13, 0.11, 0.9), ("G5", 0.26, 0.11, 0.95), ("C6", 0.42, 0.7, 1.0)]
    ch = [(0.42, 0.7, ["C4", "E4", "G4"])]
    tr = fanfare(mel, ch, 1.8)
    timpani(tr, 0.0, 98, 0.9); timpani(tr, 0.42, 130.8, 1.0)
    sparkle(tr, 0.45, ["C7", "E7", "G7"], 0.06, 0.12, 0.6)
    return room(tr.x, 0.18, 0.6, 21)


def se_rank_up():
    mel = [("G4", 0.00, 0.1, 0.85), ("C5", 0.12, 0.1, 0.9), ("E5", 0.24, 0.1, 0.9), ("G5", 0.36, 0.28, 0.95),
           ("E5", 0.68, 0.1, 0.85), ("G5", 0.80, 0.1, 0.9), ("C6", 0.92, 0.1, 0.95), ("E6", 1.04, 0.9, 1.0)]
    ch = [(0.36, 0.3, ["C4", "E4", "G4"]), (1.04, 1.0, ["C4", "G4", "C5", "E5"])]
    tr = fanfare(mel, ch, 2.6)
    for t0, f, a in ((0.0, 98, 0.8), (0.36, 130.8, 0.9), (0.68, 98, 0.7), (1.04, 130.8, 1.0)):
        timpani(tr, t0, f, a)
    sparkle(tr, 1.08, ["C7", "E7", "G7", "C8"], 0.075, 0.12, 0.8)
    return room(tr.x, 0.2, 0.8, 22)


def se_clear():
    seq = [  # (t0, dur, melody notes [(name, offset)], chord)
        (0.00, 0.60, [("C5", 0.00), ("E5", 0.11), ("G5", 0.22), ("C6", 0.33)], ["C4", "E4", "G4"]),
        (0.62, 0.60, [("F5", 0.00), ("A5", 0.11), ("C6", 0.22), ("F6", 0.33)], ["F3", "A3", "C4"]),
        (1.24, 0.60, [("G5", 0.00), ("B5", 0.11), ("D6", 0.22), ("G6", 0.33)], ["G3", "B3", "D4"]),
    ]
    tr = Track()
    for t0, d, ns, ch in seq:
        for n, off in ns:
            f = hz(midi(n))
            tr.add(horn(f, 0.2 if off < 0.3 else 0.35, 0.9), t0 + off, 0.8)
            tr.add(pluck(f, 0.7, 0.9, 3.0), t0 + off, 0.22)
        for i, n in enumerate(ch):
            tr.add(horn(hz(midi(n)), d, 0.5), t0 + i * 0.012, 0.3)
        timpani(tr, t0, hz(midi(ch[0])) / 2 if hz(midi(ch[0])) > 150 else hz(midi(ch[0])), 0.8)
    # final C major: big chord + melody on top
    t0 = 1.9
    for n, off, d in (("C6", 0.0, 1.5), ("E6", 0.0, 1.5), ("G5", 0.0, 1.5), ("C5", 0.0, 1.5), ("G4", 0.0, 1.5), ("C4", 0.0, 1.5)):
        f = hz(midi(n))
        tr.add(horn(f, d, 0.7), t0, 0.34)
        tr.add(pluck(f, 1.6, 0.9, 2.0), t0 + 0.02, 0.2)
    timpani(tr, t0, 65.4, 1.0); timpani(tr, t0 + 0.12, 98, 0.7)
    sparkle(tr, t0 + 0.05, ["C6", "E6", "G6", "C7", "E7", "G7", "C8"], 0.08, 0.16, 1.2)
    return room(tr.x, 0.24, 1.0, 23)


def se_gameover():
    tr = Track()
    seq = (("A4", 0.00, 0.4), ("G4", 0.42, 0.4), ("E4", 0.84, 0.4), ("A3", 1.30, 1.0))
    for n, t0, d in seq:
        f = hz(midi(n))
        tr.add(pluck(f, d + 0.6, 0.7, 2.2), t0, 0.7)
        tr.add(horn(f / 2 if n == "A3" else f, d, 0.4), t0, 0.25)
    for i, n in enumerate(("A2", "E3", "A3", "C4")):
        tr.add(pad_note(hz(midi(n)), 1.7, 40 + i), 1.2, 0.16)
    return room(tr.x, 0.26, 0.8, 24)


def se_shop_open():
    tr = Track()
    r = np.random.default_rng(31)
    t = tarr(0.05)
    tr.add(lp(r.standard_normal(len(t)), 900) * np.exp(-t * 90) + np.sin(2 * np.pi * 95 * t) * np.exp(-t * 60), 0.0, 0.55)   # latch
    for t0, f, a in ((0.07, 2637, 1.0), (0.24, 2093, 0.85)):
        tr.add(bell(f, 0.8, (1, 2.43, 4.1, 6.3), (1, .45, .25, .12), (5.0, 7.5, 10, 13)), t0, a)
    return room(tr.x, 0.16, 0.45, 32)


def se_day_start():
    tr = Track()
    for i, n in enumerate(("E5", "G5", "B5", "E6")):
        f = hz(midi(n))
        tr.add(bell(f, 1.0, (1, 2.0, 3.0), (1, .26, .1), (3.4, 5.5, 8.0)), i * 0.14, 0.7)
        tr.add(pluck(f, 0.6, 0.85, 3.5), i * 0.14, 0.2)
    return room(tr.x, 0.22, 0.7, 33)


def se_upgrade():
    tr = Track()
    anvil_hit(0.0, tr, 0.55, 1.3, 41)
    for i, n in enumerate(("G5", "B5", "D6", "G6")):
        f = hz(midi(n))
        tr.add(bell(f, 0.8, (1, 2.0, 3.0), (1, .3, .12), (3.6, 5.5, 8.0)), 0.16 + i * 0.075, 0.55)
    tr.add(bell(2200, 0.25, (1, 1.52, 2.34), (1, .5, .3), (14, 20, 28)), 0.05, 0.4)
    return room(tr.x, 0.18, 0.5, 42)


SE_TABLE = [  # file, builder, peak
    ("ui_click",   se_ui_click,                  0.24),
    ("ui_nav",     se_ui_nav,                    0.22),
    ("ui_back",    se_ui_back,                   0.22),
    ("craft",      lambda: se_craft(False),      0.80),
    ("craft_rare", lambda: se_craft(True),       0.80),
    ("coin",       se_coin,                      0.55),
    ("upgrade",    se_upgrade,                   0.62),
    ("sell",       se_sell,                      0.62),
    ("error",      se_error,                     0.30),
    ("contract",   se_contract,                  0.78),
    ("rank_up",    se_rank_up,                   0.80),
    ("clear",      se_clear,                     0.82),
    ("gameover",   se_gameover,                  0.55),
    ("shop_open",  se_shop_open,                 0.55),
    ("day_start",  se_day_start,                 0.45),
]


def write_wav(path, x):
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(pcm.tobytes())


# ----------------------------------------------------------------------------------------------------------------
# BGM  "Hearth and Anvil"
# ----------------------------------------------------------------------------------------------------------------
BPM = 96
BEAT = SR * 60.0 / BPM                    # 27562.5
BAR = int(round(BEAT * 4))                # 110250
BARS = 32
N = BAR * BARS                            # 3 528 000 = 80.000 s


def bpos(bar, beat):
    return int(round((bar * 4 + beat) * BEAT))


class Bus:
    def __init__(self):
        self.x = np.zeros((2, N), dtype=np.float32)

    def put(self, sig, start, pan=0.0, g=1.0):
        """add a mono signal at `start` (samples) with equal-power pan; wraps around the loop end"""
        n = len(sig)
        start %= N
        gl, gr = np.cos((pan + 1) * np.pi / 4) * g, np.sin((pan + 1) * np.pi / 4) * g
        k = min(n, N - start)
        self.x[0, start:start + k] += sig[:k] * gl
        self.x[1, start:start + k] += sig[:k] * gr
        if k < n:                                                  # the tail continues at the beginning of the loop
            m = min(n - k, N)
            self.x[0, :m] += sig[k:k + m] * gl
            self.x[1, :m] += sig[k:k + m] * gr


# chord table: pad voicing, arpeggio voicing (ascending), bass root / fifth (MIDI)
CHORDS = {
    "Am": dict(pad=[57, 60, 64], arp=[57, 60, 64, 69], root=45, fifth=52),
    "F":  dict(pad=[57, 60, 65], arp=[53, 57, 60, 65], root=41, fifth=48),
    "C":  dict(pad=[55, 60, 64], arp=[55, 60, 64, 67], root=48, fifth=43),
    "G":  dict(pad=[55, 59, 62], arp=[55, 59, 62, 67], root=43, fifth=50),
    "E":  dict(pad=[56, 59, 64], arp=[52, 56, 59, 64], root=40, fifth=47),
    "Em": dict(pad=[55, 59, 64], arp=[52, 55, 59, 64], root=40, fifth=47),
    "Dm": dict(pad=[57, 62, 65], arp=[50, 57, 62, 65], root=38, fifth=45),
}
PROG = (["Am", "F", "C", "G", "Am", "F", "G", "E"] +                     # A    (bars  0- 7)
        ["Am", "F", "C", "G", "Am", "F", "G", "E"] +                     # A'   (bars  8-15)
        ["F", "G", "Am", "Em", "F", "C", "G", "E"] +                     # B    (bars 16-23)
        ["Am", "F", "C", "G", "Am", "F", "G", "Dm"])                     # A''  (bars 24-31)

# melody: per bar a list of (note, start beat, length in beats); None = rest
MEL_A = [
    [("E5", 0, 1), ("A5", 1, .5), ("G5", 1.5, .5), ("E5", 2, 1), ("C5", 3, 1)],
    [("C5", 0, 1), ("D5", 1, .5), ("C5", 1.5, .5), ("A4", 2, 2)],
    [("E5", 0, 1), ("G5", 1, 1), ("E5", 2, .5), ("D5", 2.5, .5), ("C5", 3, 1)],
    [("D5", 0, 1), ("E5", 1, .5), ("D5", 1.5, .5), ("B4", 2, 1), ("D5", 3, 1)],
    [("E5", 0, 1), ("A5", 1, 1), ("C6", 2, 1), ("A5", 3, 1)],
    [("A5", 0, 1), ("G5", 1, 1), ("F5", 2, 1), ("E5", 3, 1)],
    [("D5", 0, 1), ("G5", 1, 1), ("B5", 2, 1), ("A5", 3, .5), ("G5", 3.5, .5)],
    [("E5", 0, 3)],
]
MEL_A2 = [
    [("A5", 0, 1.5), ("G5", 1.5, .5), ("E5", 2, 1), ("G5", 3, .5), ("A5", 3.5, .5)],
    [("C6", 0, 1), ("A5", 1, 1), ("F5", 2, 1.5), ("E5", 3.5, .5)],
    [("G5", 0, 1), ("E5", 1, .5), ("G5", 1.5, .5), ("C6", 2, 1.5), ("B5", 3.5, .5)],
    [("G5", 0, 1), ("B5", 1, .5), ("A5", 1.5, .5), ("G5", 2, 1), ("D5", 3, 1)],
    [("E5", 0, .5), ("A5", .5, .5), ("C6", 1, 1), ("E6", 2, 1.5), ("D6", 3.5, .5)],
    [("C6", 0, 1.5), ("A5", 1.5, .5), ("F5", 2, 1), ("A5", 3, 1)],
    [("B5", 0, 1), ("D6", 1, 1), ("B5", 2, .5), ("A5", 2.5, .5), ("G5", 3, 1)],
    [("B5", 0, 3)],
]
MEL_B = [
    [("A5", 0, .5), ("C6", .5, .5), ("A5", 1, .5), ("G5", 1.5, .5), ("F5", 2, 1), ("A5", 3, 1)],
    [("G5", 0, .5), ("B5", .5, .5), ("D6", 1, 1), ("B5", 2, .5), ("A5", 2.5, .5), ("G5", 3, 1)],
    [("E5", 0, .5), ("A5", .5, .5), ("C6", 1, .5), ("B5", 1.5, .5), ("A5", 2, 1.5), ("E5", 3.5, .5)],
    [("G5", 0, 1), ("B5", 1, 1), ("E6", 2, 1), ("D6", 3, 1)],
    [("C6", 0, 1), ("A5", 1, .5), ("C6", 1.5, .5), ("F6", 2, 1.5), ("E6", 3.5, .5)],
    [("E6", 0, 1), ("D6", 1, .5), ("C6", 1.5, .5), ("G5", 2, 1), ("E5", 3, 1)],
    [("D5", 0, .5), ("G5", .5, .5), ("B5", 1, .5), ("D6", 1.5, .5), ("D6", 2, 1.5), ("B5", 3.5, .5)],
    [("B5", 0, 1), ("G#5", 1, 1), ("A5", 2, .5), ("G#5", 2.5, .5), ("E5", 3, 1)],
]
MEL_END = [("D5", 0, 1), ("F5", 1, 1), ("A5", 2, 1), ("G5", 3, 1)]          # last bar (Dm): leads back to E5 of bar 0
MELODY = MEL_A + MEL_A2 + MEL_B + MEL_A[:7] + [MEL_END]

ARP_PAT_A = [0, 2, 1, 3, 2, 1, 2, 1]
ARP_PAT_B = [0, 1, 2, 3, 2, 3, 2, 1]
SECTION_LEVEL = [0.92] * 8 + [1.0] * 8 + [1.04] * 8 + [0.9] * 8


def jit(r, ms=5):
    return int(r.uniform(-ms, ms) * SR / 1000.0)


def render_bgm():
    r = np.random.default_rng(777)
    arp, mel, pad, bass, perc = Bus(), Bus(), Bus(), Bus(), Bus()
    for bar in range(BARS):
        ch = CHORDS[PROG[bar]]
        sec = bar // 8
        lvl = SECTION_LEVEL[bar]
        # --- lute arpeggio (eighth notes) ---
        pat = ARP_PAT_B if sec == 2 else ARP_PAT_A
        for i, idx in enumerate(pat):
            f = hz(ch["arp"][idx])
            v = (1.0 if i in (0, 4) else 0.66 if i % 2 == 0 else 0.5) * lvl * r.uniform(0.9, 1.05)
            arp.put(pluck(f, 1.5, 0.82, 2.9, seed=bar * 8 + i), bpos(bar, i * 0.5) + jit(r, 4), pan=-0.28, g=v)
        # --- bass: root on 1, fifth on 3, light walk on 4& in sections A' and B ---
        bass.put(bass_note(hz(ch["root"]), 1.9 * BEAT / SR), bpos(bar, 0) + jit(r, 3), 0.0, 0.95 * lvl)
        bass.put(bass_note(hz(ch["fifth"]), 1.2 * BEAT / SR), bpos(bar, 2) + jit(r, 3), 0.0, 0.68 * lvl)
        if sec in (1, 2):
            bass.put(bass_note(hz(ch["root"] + 12), 0.5 * BEAT / SR), bpos(bar, 3.5) + jit(r, 3), 0.0, 0.34 * lvl)
        # --- pad: a chord per bar, overlapped (0.6 s) so the changes glide ---
        pd = (4 * BEAT / SR) + 0.9
        pg = {0: 0.55, 1: 0.8, 2: 0.9, 3: 0.65}[sec]
        for j, m in enumerate(ch["pad"]):
            f = hz(m)
            note = pad_note(f, pd, seed=bar * 3 + j)
            pad.put(note, bpos(bar, 0) - int(0.45 * SR), pan=(-0.5, 0.0, 0.5)[j], g=pg)
        # --- melody: flute (+ an octave-up lute echo in B, lute doubling in A'') ---
        for (n, b0, d) in MELODY[bar]:
            f = hz(midi(n))
            dur = max(0.12, d * BEAT / SR * 0.94)
            vel = (0.92 if b0 in (0, 2) else 0.8) * lvl * r.uniform(0.92, 1.05)
            st = bpos(bar, b0) + jit(r, 5)
            if sec == 3:                                    # A'': lute carries the tune, flute joins softly
                mel.put(pluck(f, 1.2, 0.9, 3.0, seed=bar * 9), st, 0.18, 0.55 * vel)
                mel.put(flute(f, dur, 0.55 * vel, seed=bar), st, 0.1, 0.5)
            else:
                mel.put(flute(f, dur, vel, seed=bar * 5), st, 0.12, 0.62 if sec == 2 else 0.7)
            if sec == 2:
                mel.put(pluck(f * 2, 0.9, 0.8, 4.2, seed=bar * 11), st, 0.25, 0.16 * vel)
        # --- percussion: frame drum, shaker, tiny metal tinks ---
        if bar >= 4 and not (bar >= 30):
            dv = 0.85 if sec != 3 else 0.6
            for b0, a in ((0, 1.0), (2, 0.55)):
                t = tarr(0.32)
                hit = np.sin(2 * np.pi * (62 + 70 * np.exp(-t * 40)) * t) * np.exp(-t * 16)
                hit += lp(r.standard_normal(len(t)), 700) * np.exp(-t * 85) * 0.5
                perc.put(fade(hit, 0.002, 0.08), bpos(bar, b0) + jit(r, 3), 0.0, 0.5 * a * dv)
            if sec in (1, 2):
                hit = np.sin(2 * np.pi * (70 + 50 * np.exp(-tarr(0.2) * 40)) * tarr(0.2)) * np.exp(-tarr(0.2) * 22)
                perc.put(fade(hit, 0.002, 0.05), bpos(bar, 3.5), 0.0, 0.2)
        if sec in (1, 2) or (sec == 3 and bar < 28):
            for i in range(8):
                t = tarr(0.06)
                sh = hp(r.standard_normal(len(t)), 5200) * np.exp(-t * 85) * np.minimum(1, t / 0.004)
                perc.put(fade(sh, 0.001, 0.01), bpos(bar, i * 0.5) + jit(r, 3), 0.35, (0.1 if i % 2 == 0 else 0.15) * lvl)
        if sec == 2:                                        # the smithy: a faint hammer "tink" on 2 and 4
            for b0 in (1, 3):
                perc.put(bell(1750, 0.18, (1, 1.51, 2.33), (1, .5, .3), (30, 45, 60)), bpos(bar, b0) + jit(r, 3), -0.35, 0.05)
    return arp, mel, pad, bass, perc


def circ_reverb(bus, wet, rt=1.5, seed=5, pre=0.018, lpf=3800):
    """circular convolution: the reverb tail of the end of the loop flows into the start (and vice versa)"""
    r = np.random.default_rng(seed)
    n = int(rt * 1.1 * SR)
    out = np.zeros_like(bus)
    for ch in range(2):
        ir = r.standard_normal(n) * np.exp(-np.arange(n) / SR * 6.9 / rt)
        ir = lp(ir, lpf)
        ir[: int(pre * SR)] = 0
        ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
        irf = np.zeros(N)
        irf[:n] = ir
        out[ch] = np.fft.irfft(np.fft.rfft(bus[ch].astype(np.float64)) * np.fft.rfft(irf), N)
    return (bus * (1 - wet * 0.35) + out * wet).astype(np.float32)


def master_bgm():
    arp, mel, pad, bass, perc = render_bgm()
    mix = np.zeros((2, N), dtype=np.float32)
    wet_part = arp.x * 0.62 + mel.x * 0.95 + pad.x * 0.34
    wet_part = circ_reverb(wet_part, 0.34, 1.7, 5)
    mix += wet_part
    mix += bass.x * 0.60
    mix += circ_reverb(perc.x, 0.15, 0.7, 9) * 0.52
    mix = mix.astype(np.float64)
    for ch in range(2):                                                # circular high-pass (25 Hz) + gentle top roll-off
        spec = np.fft.rfft(mix[ch])
        fr = np.fft.rfftfreq(N, 1 / SR)
        h = 1.0 / np.sqrt(1 + (25.0 / np.maximum(fr, 1e-3)) ** 4) * 1.0 / np.sqrt(1 + (fr / 11000.0) ** 4)
        mix[ch] = np.fft.irfft(spec * h, N)
    rms = np.sqrt(np.mean(mix ** 2))
    mix *= (10 ** (-15.0 / 20)) / rms                                  # -15 dBFS RMS
    mix = np.tanh(mix * 1.15) / 1.15                                   # gentle safety limiter, no hard clipping
    pk = np.max(np.abs(mix))
    if pk > 0.89:
        mix *= 0.89 / pk
    return mix.astype(np.float32)


def seam_report(x, name):
    """x: (2,N) or (N,) loop.  Compare the jump last->first sample with typical neighbour differences."""
    x = np.atleast_2d(x)
    d = np.abs(np.diff(x, axis=1))
    typ99 = np.percentile(d, 99.9)
    jump = np.abs(x[:, 0] - x[:, -1]).max()
    return f"{name}: seam jump {jump:.5f}  (99.9th percentile of normal sample steps {typ99:.5f})"


def main():
    os.makedirs(os.path.join(OUT, "se"), exist_ok=True)
    print("== sound effects ==")
    for name, fn, peak in SE_TABLE:
        x = finish(fn(), peak)
        rms = np.sqrt(np.mean(x ** 2))
        print(f"  {name:11s} {len(x) / SR:5.2f} s  peak {db(np.max(np.abs(x))):6.1f} dBFS  rms {db(rms):6.1f} dBFS  "
              f"first {abs(x[0]):.4f} last {abs(x[-1]):.4f}")
        if not CHECK_ONLY:
            write_wav(os.path.join(OUT, "se", name + ".wav"), x)
    print("== bgm ==")
    bgm = master_bgm()
    print(f"  {N} samples = {N / SR:.3f} s, peak {db(np.max(np.abs(bgm))):.1f} dBFS, rms {db(np.sqrt(np.mean(bgm.astype(np.float64) ** 2))):.1f} dBFS")
    print("  " + seam_report(bgm, "float loop"))
    if not CHECK_ONLY:
        pcm = np.clip(np.round(bgm.T * 32767), -32768, 32767).astype("<i2")
        with tempfile.TemporaryDirectory() as td:
            wav = os.path.join(td, "bgm.wav")
            with wave.open(wav, "wb") as w:
                w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
                w.writeframes(pcm.tobytes())
            dst = os.path.join(OUT, "bgm_hearth.ogg")
            subprocess.check_call(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", "5",
                                   "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", dst])
        print("  wrote", os.path.relpath(dst))
        np.save(os.path.join(tempfile.gettempdir(), "lootsmith_bgm_ref.npy"), bgm)      # reference for the loop check


if __name__ == "__main__":
    main()
