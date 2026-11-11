# SPDX-License-Identifier: MIT
"""Where the sound sits between left and right, and how loud it is, per stretch of a recording made by
mevr-audio-loopback (research/69 "The voices, placed"): a voice placed to the listener's right is louder in the right
channel; one the game plays "centred" is equal in both.

    python tools/audio-balance.py <recording.wav> [--window 1.0] [--from S] [--to S] [--quiet -50]

Per window: the level of each of the first two channels in dB (full scale), and the balance (right minus left, dB;
positive: to the right). Windows quieter than --quiet are printed as silence. Then the whole stretch, over the windows
that were not silent, and the delay between the ears: the shift of the right channel against the left (within a
millisecond) at which the two are most alike, below 1.5 kHz. Plain panning has none (0.00 ms); a head-related filter
delays the far ear by up to 0.7 ms.
"""
import argparse
import struct
import sys

import numpy as np


def read_wav(path):
    data = open(path, 'rb').read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise SystemExit('not a WAV file')
    at = 12
    fmt = None
    while at + 8 <= len(data):
        tag, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
        body = data[at + 8:at + 8 + size]
        if tag == b'fmt ':
            kind, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', body)
            fmt = (kind, channels, rate, bits)
        elif tag == b'data':
            if fmt is None:
                raise SystemExit('no format before the data')
            kind, channels, rate, bits = fmt
            if kind == 3 and bits == 32:
                samples = np.frombuffer(body, dtype='<f4')
            elif kind == 1 and bits == 16:
                samples = np.frombuffer(body, dtype='<i2').astype(np.float32) / 32768.0
            else:
                raise SystemExit(f'format {kind}, {bits} bits is not read')
            return samples[:len(samples) // channels * channels].reshape(-1, channels), rate
        at += 8 + size + (size & 1)
    raise SystemExit('no data')


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-9))


def ear_delay(frames, rate):
    """(ms the left channel lags the right: positive = the right ear hears it first, likeness there, likeness at 0)."""
    n = max(1, int(rate / 3000))  # a moving average: about 1.5 kHz and below
    kernel = np.ones(n) / n
    left = np.convolve(frames[:, 0].astype(np.float64), kernel, mode='same')
    right = np.convolve(frames[:, 1].astype(np.float64), kernel, mode='same')
    left -= left.mean()
    right -= right.mean()
    norm = float(np.sqrt(np.sum(left ** 2) * np.sum(right ** 2)))
    if norm <= 0.0:
        return 0.0, 0.0, 0.0
    reach = int(rate / 1000)
    best, best_lag, at_zero = -2.0, 0, 0.0
    for lag in range(-reach, reach + 1):
        if lag >= 0:
            c = float(np.sum(left[lag:] * right[:len(right) - lag])) / norm
        else:
            c = float(np.sum(left[:lag] * right[-lag:])) / norm
        if lag == 0:
            at_zero = c
        if c > best:
            best, best_lag = c, lag
    return best_lag * 1000.0 / rate, best, at_zero


def main():
    p = argparse.ArgumentParser()
    p.add_argument('wav')
    p.add_argument('--window', type=float, default=1.0)
    p.add_argument('--from', dest='start', type=float, default=0.0)
    p.add_argument('--to', dest='end', type=float, default=1e9)
    p.add_argument('--quiet', type=float, default=-50.0)
    a = p.parse_args()
    frames, rate = read_wav(a.wav)
    if frames.shape[1] < 2:
        raise SystemExit('one channel: no balance to measure')
    step = max(1, int(a.window * rate))
    left_sum = right_sum = 0.0
    count = 0
    for i in range(int(a.start * rate), min(len(frames), int(min(a.end, 1e7) * rate)), step):
        w = frames[i:i + step]
        if len(w) < step // 2:
            break
        left = float(np.sqrt(np.mean(w[:, 0].astype(np.float64) ** 2)))
        right = float(np.sqrt(np.mean(w[:, 1].astype(np.float64) ** 2)))
        t = i / rate
        if max(db(left), db(right)) < a.quiet:
            print(f'{t:6.1f} s  silence')
            continue
        left_sum += left ** 2 * len(w)
        right_sum += right ** 2 * len(w)
        count += len(w)
        print(f'{t:6.1f} s  left {db(left):6.1f} dB  right {db(right):6.1f} dB  balance {db(right) - db(left):+5.1f} dB')
    if count:
        left, right = (left_sum / count) ** 0.5, (right_sum / count) ** 0.5
        print(f'whole: left {db(left):.1f} dB, right {db(right):.1f} dB, balance {db(right) - db(left):+.1f} dB '
              f'({count / rate:.1f} s with sound)')
        stretch = frames[int(a.start * rate):min(len(frames), int(min(a.end, 1e7) * rate))]
        ms, likeness, at_zero = ear_delay(stretch, rate)
        print(f'delay: the left channel {abs(ms):.2f} ms {"behind" if ms > 0 else "ahead of" if ms < 0 else "with"} the right '
              f'(likeness {likeness:.2f} there, {at_zero:.2f} with none)')
    else:
        print('whole: silence')


if __name__ == '__main__':
    sys.exit(main())
