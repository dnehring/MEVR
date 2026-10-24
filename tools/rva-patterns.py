# SPDX-License-Identifier: MIT
"""Byte patterns for engine/Addresses.hpp (research/34 stage 4), from the decrypted .text dump.

The host saves the decrypted .text once per exe build (engine/CodeDump: %LOCALAPPDATA%\\MEVR\\dump\\
MassEffect-<timestamp>.text.bin, with a .txt giving text_rva). For each address this prints a pattern that matches
exactly once in .text, with rel32 jumps/calls and RIP-relative displacements wildcarded (a heuristic: a wrongly
wildcarded byte only makes the pattern less specific, and uniqueness is checked), as an engine/Addresses.hpp entry:

    function  the function's first bytes (offset 0); a function start in .pdata if the exe is given
    code      bytes around an address inside a function (offset = where the address is in the pattern)
    end       the last bytes before a function's end (offset = the pattern's length)
    data      an instruction that references the global RIP-relatively, with its displacement wildcarded
              (offset = the displacement's place; site = where the pattern starts)

    python -I tools/rva-patterns.py <dump.text.bin> [--exe MassEffect1.exe] name=0x12DD20:function ...

Read-only. Local research data (the dump) never leaves the machine; the printed patterns are ours.
"""
import bisect
import os
import re
import struct
import sys

IMAGE_LIMIT = 0x2000000  # RIP targets beyond this aren't inside the image (LE1's is 0x1DB8000)


def load_text(path):
    data = open(path, 'rb').read()
    info = os.path.splitext(os.path.splitext(path)[0])[0] + '.txt'
    text_rva = 0x1000
    if os.path.exists(info):
        for line in open(info):
            key, _, value = line.strip().partition('=')
            if key == 'text_rva':
                text_rva = int(value, 16)
    return data, text_rva


def load_pdata(exe):
    d = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    exc_rva, exc_size = struct.unpack_from('<II', d, opt + 112 + 3 * 8)
    secs = []
    for i in range(nsec):
        o = opt + optsz + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
        secs.append((va, max(vsize, rsize), raw))
    base = next(raw + exc_rva - va for va, size, raw in secs if va <= exc_rva < va + size)
    return sorted(struct.unpack_from('<II', d, base + i * 12)[:2] for i in range(exc_size // 12))


class Text:
    def __init__(self, data, rva):
        self.data = data
        self.rva = rva
        self._wild = {}

    def byte(self, rva):
        return self.data[rva - self.rva]

    def i32(self, rva):
        return struct.unpack_from('<i', self.data, rva - self.rva)[0]

    def wild(self, rva):
        """Whether the byte at `rva` is probably part of a rel32 or a RIP-relative displacement."""
        if rva in self._wild:
            return self._wild[rva]
        result = False
        for start in range(rva - 3, rva + 1):  # a 4-byte field starting here would cover rva
            if start - 2 < self.rva:
                continue
            prev = self.byte(start - 1)
            prev2 = self.byte(start - 2)
            if prev in (0xE8, 0xE9) or (prev2 == 0x0F and 0x80 <= prev <= 0x8F):
                result = True
            elif (prev & 0xC7) == 0x05:
                target = start + 4 + self.i32(start)
                if 0 < target < IMAGE_LIMIT:
                    result = True
            if result:
                break
        self._wild[rva] = result
        return result

    def pattern(self, start, length, forced=()):
        out = []
        for rva in range(start, start + length):
            out.append('??' if rva in forced or self.wild(rva) else f'{self.byte(rva):02X}')
        return ' '.join(out)

    def count(self, pattern, limit=2):
        regex = re.compile(b''.join(b'.' if t == '??' else re.escape(bytes([int(t, 16)])) for t in pattern.split()), re.DOTALL)
        n = 0
        for _ in regex.finditer(self.data):
            n += 1
            if n >= limit:
                break
        return n


def concrete(pattern):
    return sum(1 for t in pattern.split() if t != '??')


def unique(text, start, max_len, forced=(), min_len=16):
    for length in range(min_len, max_len + 1, 4):
        p = text.pattern(start, length, forced)
        if concrete(p) >= 12 and text.count(p) == 1:
            return p
    return None


def data_sites(text, rva):
    """Positions of disp32 fields whose instruction (ending right after them) targets `rva`."""
    import numpy as np
    raw = np.frombuffer(text.data, dtype=np.uint8).astype(np.int64)
    n = len(raw) - 3
    disp = raw[0:n] | (raw[1:n + 1] << 8) | (raw[2:n + 2] << 16) | (raw[3:n + 3] << 24)
    disp = np.where(disp >= 1 << 31, disp - (1 << 32), disp)
    positions = np.arange(n, dtype=np.int64) + text.rva
    hits = np.nonzero(positions + 4 + disp == rva)[0]
    return [int(text.rva + p) for p in hits]


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    data, text_rva = load_text(args.pop(0))
    text = Text(data, text_rva)
    funcs = None
    if args and args[0] == '--exe':
        args.pop(0)
        funcs = load_pdata(args.pop(0))
    starts = [f[0] for f in funcs] if funcs else []
    for arg in args:
        name, _, spec = arg.partition('=')
        value, _, kind = spec.partition(':')
        rva = int(value, 16)
        note = ''
        if funcs:
            i = bisect.bisect_right(starts, rva - (1 if kind == 'end' else 0)) - 1
            f = funcs[i] if i >= 0 else None
            if kind == 'function':
                note = 'pdata start' if f and f[0] == rva else 'NOT a pdata start'
            elif kind == 'end':
                note = 'pdata end' if f and f[1] == rva else 'NOT a pdata end'
            elif kind == 'code':
                note = f'inside 0x{f[0]:X}' if f and f[0] <= rva < f[1] else 'NOT inside a function'
        if kind == 'function':
            p = unique(text, rva, 64)
            print(f'{name}: rva=0x{rva:X} offset=0 site=0x{rva:X} [{note}] "{p}"')
        elif kind == 'code':
            for before in range(8, 33, 4):
                p = unique(text, rva - before, 64, min_len=before + 8)
                if p:
                    break
            print(f'{name}: rva=0x{rva:X} offset={before} site=0x{rva - before:X} [{note}] "{p}"')
        elif kind == 'end':
            p = None
            for length in range(16, 65, 4):
                p = text.pattern(rva - length, length)
                if concrete(p) >= 12 and text.count(p) == 1:
                    break
                p = None
            print(f'{name}: rva=0x{rva:X} offset={length} site=0x{rva - length:X} [{note}] "{p}"')
        elif kind == 'data':
            best = None
            seen = set()
            for site in data_sites(text, rva):
                forced = set(range(site, site + 4))
                window = text.pattern(site - 16, 40, forced)
                if window in seen:  # one of many identical sites (registration stubs): never unique
                    continue
                seen.add(window)
                for before in (8, 12, 16):
                    p = unique(text, site - before, 48, forced, min_len=before + 8)
                    if p and (best is None or len(p) < len(best[2])):
                        best = (site, before, p)
                        break
            if best is None:
                print(f'{name}: rva=0x{rva:X} data: no unique reference')
            else:
                site, before, p = best
                print(f'{name}: rva=0x{rva:X} offset={before} site=0x{site - before:X} "{p}"')
        else:
            print(f'{name}: unknown kind {kind!r}')


if __name__ == '__main__':
    main()
