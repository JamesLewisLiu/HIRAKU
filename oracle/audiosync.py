#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""How well a HIRAKU recording keeps time with a MAME one of the same run.

    oracle/audiosync.py HIRAKU.wav MAME.wav [--from S] [--to S] [--offset S]

For machines that don't run in lockstep with MAME (Firebeat), where the music
has to stay in step instead: CD audio, DVD-streamed BGM.
1. The overall offset (HIRAKU usually starts ahead: it skips most of the BIOS),
   from the loudness envelopes, then refined on the waveform within +-2 s:
   music repeating every few beats can fool the envelope match.
2. In 2 s windows of MAME's time: the lag in samples, the waveform
   correlation and the level ratio. A constant lag means no drift.
For the timing, MAME's recording is resampled to 44.1 kHz; levels are measured at
each file's own rate. Both are mixed to mono.
"""
import argparse
import wave

import numpy as np

RATE = 44100


def load(path):
    """Mono at 44.1 kHz (for the timing), and mono at the file's own rate (for levels:
    linear resampling dulls the top octaves, which shows as a level difference)."""
    w = wave.open(path)
    r, c = w.getframerate(), w.getnchannels()
    native = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, c)[:, :2].astype(np.float64).mean(1) / 32768
    x = native
    if r != RATE:
        x = np.interp(np.arange(int(len(x) * RATE / r)) / RATE, np.arange(len(x)) / r, x)
    return x, native, r


def rms(x, rate, t, seconds):
    seg = x[int(t * rate):int((t + seconds) * rate)]
    return float(np.sqrt((seg ** 2).mean())) if len(seg) else 0.0


def envelope(x, ms=10):
    b = RATE * ms // 1000
    n = len(x) // b
    return np.sqrt((x[:n * b] ** 2).reshape(n, b).mean(1))


def coarse_offset(h, m):
    """HIRAKU time - MAME time of the same moment, from 10 ms envelopes (seconds)."""
    eh, em = envelope(h), envelope(m)
    best = (-2.0, 0)
    for lag in range(-6000, 6001, 2):
        lo, hi = max(0, -lag), min(len(em), len(eh) - lag)
        if hi - lo < 1000:
            continue
        a, b = em[lo:hi], eh[lo + lag:hi + lag]
        if a.std() < 1e-6 or b.std() < 1e-6:
            continue
        c = np.corrcoef(a, b)[0, 1]
        if c > best[0]:
            best = (c, lag)
    return best[1] / 100


def xcorr_peak(ref, seg):
    """Best position of seg within ref (FFT), and the normalised correlation there."""
    n = len(ref) + len(seg)
    c = np.fft.irfft(np.fft.rfft(ref, n) * np.conj(np.fft.rfft(seg, n)), n)[:len(ref) - len(seg) + 1]
    cs = np.concatenate([[0], np.cumsum(ref ** 2)])
    e = np.sqrt(np.maximum(cs[len(seg):] - cs[:-len(seg)], 0)) * np.linalg.norm(seg) + 1e-12
    k = int(np.argmax(c / e))
    return k, float(c[k] / e[k])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hiraku")
    ap.add_argument("mame")
    ap.add_argument("--from", dest="t0", type=float, default=None, help="MAME time to start at (default: first sound)")
    ap.add_argument("--to", dest="t1", type=float, default=None)
    ap.add_argument("--offset", type=float, default=None, help="HIRAKU minus MAME time, seconds (skip the search)")
    a = ap.parse_args()
    (h, hn, hr), (m, mn, mr) = load(a.hiraku), load(a.mame)

    if a.offset is None:
        off = coarse_offset(h, m)
        # refine on the loudest 4 s of MAME's recording, within +-2 s of the envelope's answer
        em = envelope(m, 1000)
        t = int(np.argmax(em[:max(1, len(em) - 6)]))
        seg = m[t * RATE:(t + 4) * RATE]
        lo = max(0, int((t + off - 2) * RATE))
        k, c = xcorr_peak(h[lo:lo + 8 * RATE], seg)
        off = (lo + k) / RATE - t
        print(f"offset: HIRAKU is {abs(off):.4f} s {'ahead of' if off < 0 else 'behind'} MAME (waveform corr {c:.3f})")
    else:
        off = a.offset
    t0 = a.t0
    if t0 is None:
        loud = np.nonzero(envelope(m, 1000) > 1e-3)[0]
        t0 = float(loud[0]) if len(loud) else 0.0
    t1 = a.t1 if a.t1 is not None else len(m) / RATE - 2
    W, R = 2 * RATE, RATE // 20
    lags = []
    for t in np.arange(t0, t1, 2.0):
        seg = m[int(t * RATE):int(t * RATE) + W]
        b0 = int((t + off) * RATE)
        if seg.std() < 1e-4 or b0 - R < 0 or b0 + W + R > len(h):
            print(f"{t:7.1f} s: -")
            continue
        k, c = xcorr_peak(h[b0 - R:b0 + W + R], seg)
        lag = k - R
        lvl = rms(hn, hr, t + off + lag / RATE, 2.0) / max(1e-12, rms(mn, mr, t, 2.0))   # at native rates
        lags.append(lag)
        print(f"{t:7.1f} s: lag {lag:+5d} samples ({lag / RATE * 1000:+.2f} ms)  corr {c:.3f}  level {lvl:.2f}")
    if lags:
        print(f"lag {min(lags):+d}..{max(lags):+d} samples over {len(lags)} windows")


if __name__ == "__main__":
    main()
