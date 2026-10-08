"""Check that RVAs are real function starts, using the exe's .pdata unwind table.

ME LE's .text is encrypted on disk (EA DRM, `.ooa` section), so byte signatures only work
in-process. .pdata is plain, so function boundaries can be checked statically. Read-only.

    python -I tools/verify_rvas.py <MassEffect1.exe> CalcSceneView=0x4C5300 Draw=0x4C71A0 ...
"""
import bisect
import struct
import sys


def load(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', d, opt)[0] != 0x20B:
        sys.exit('not a PE32+ image')
    exc_rva, exc_size = struct.unpack_from('<II', d, opt + 112 + 3 * 8)
    secs = []
    for i in range(nsec):
        o = opt + optsz + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
        secs.append((d[o:o + 8].rstrip(b'\0').decode(), va, max(vsize, rsize), raw))

    def off(rva):
        for _, va, size, raw in secs:
            if va <= rva < va + size:
                return raw + rva - va
        return None

    base = off(exc_rva)
    funcs = sorted(struct.unpack_from('<II', d, base + i * 12) for i in range(exc_size // 12))
    return secs, funcs


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    secs, funcs = load(sys.argv[1])
    starts = [f[0] for f in funcs]
    print(f"sections={[s[0] for s in secs]} runtime_functions={len(funcs)}")
    for arg in sys.argv[2:]:
        name, _, value = arg.partition('=')
        rva = int(value, 16)
        i = bisect.bisect_right(starts, rva) - 1
        f = funcs[i] if i >= 0 and funcs[i][0] <= rva < funcs[i][1] else None
        if f is None:
            verdict = 'NOT inside any function'
        elif f[0] == rva:
            verdict = f'function start (0x{f[0]:X}-0x{f[1]:X})'
        else:
            verdict = f'inside function 0x{f[0]:X}-0x{f[1]:X} (+0x{rva - f[0]:X})'
        print(f'{name:28s} 0x{rva:X}: {verdict}')


if __name__ == '__main__':
    main()
