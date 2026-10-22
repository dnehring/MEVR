"""Undo hook patches in a spliced exe where LExSDKv2 records the original first bytes.

A .text dump taken while hooks were live has a 5-byte `jmp` out of the image at the start of each
hooked function (see engine/CodeDump). LExSDKv2's Init.hpp keeps commented-out post-hook patterns
for some engine functions, `FromPostHook(/* 40 55 41 56 41 */ "57 48 81 EC ...")`: the comment is
the five bytes a hook overwrites. Where the dump has a hook jmp at that function's RVA and the
bytes after it match the pattern, this writes the original five bytes back.

    python -I tools/ghidra/restore_prologues.py LE1 <spliced.exe>

Local research data only: edits the spliced copy in place, never the game's exe.
"""
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def sdk_block(game):
    text = (ROOT / 'external/LExSDKv2/Src/LESDK/Init.hpp').read_text(encoding='utf-8', errors='replace')
    blocks = re.split(r'^\s*#\s*(?:if|elif)\s+defined\((SDK_TARGET_LE\d)\)', text, flags=re.M)
    return '\n'.join(re.split(r'^\s*#\s*endif', blocks[i + 1], maxsplit=1, flags=re.M)[0]
                     for i in range(1, len(blocks), 2) if blocks[i] == f'SDK_TARGET_{game}')


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    game, exe = sys.argv[1].upper(), sys.argv[2]
    block = sdk_block(game)
    rvas = {m[0]: int(m[1], 16) for m in re.findall(r'BUILTIN_(\w+?)_RVA\s+::LESDK::Address::FromOffset\((0x[0-9A-Fa-f]+)\)', block)}
    hooks = re.findall(r'BUILTIN_(\w+?)_PHOOK\s+::LESDK::Address::FromPostHook\(/\*\s*([0-9A-Fa-f? ]+?)\s*\*/\s*"([0-9A-Fa-f? ]+)"', block)

    d = bytearray(open(exe, 'rb').read())
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    count = struct.unpack_from('<H', d, pe + 6)[0]
    opt = pe + 24
    image_size = struct.unpack_from('<I', d, opt + 56)[0]
    table = opt + struct.unpack_from('<H', d, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', d, table + i * 40 + 8) for i in range(count)]

    def offset(rva):
        for vsize, va, rsize, raw in sections:
            if va <= rva < va + min(vsize, rsize):
                return raw + rva - va
        return None

    restored = 0
    for name, first, rest in hooks:
        rva, off = rvas.get(name), None
        if rva is not None:
            off = offset(rva)
        if off is None:
            continue
        if d[off] != 0xE9 or 0 <= rva + 5 + struct.unpack_from('<i', d, off + 1)[0] < image_size:
            continue  # not hooked in this dump
        original = first.split()
        pattern = rest.split()
        if len(original) != 5 or '??' in original:
            print(f'{name} (0x{rva:X}): hooked, original bytes not fully known; left as is')
            continue
        after = d[off + 5:off + 5 + len(pattern)]
        if any(p != '??' and int(p, 16) != b for p, b in zip(pattern, after)):
            print(f'{name} (0x{rva:X}): hooked, but the following bytes differ from LExSDKv2\'s pattern; left as is')
            continue
        d[off:off + 5] = bytes(int(b, 16) for b in original)
        restored += 1
        print(f'{name} (0x{rva:X}): restored {first.strip()}')
    open(exe, 'wb').write(d)
    print(f'{restored} hook-patched prologue(s) restored from LExSDKv2')


if __name__ == '__main__':
    main()
