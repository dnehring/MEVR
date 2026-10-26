# SPDX-License-Identifier: MIT
"""Cross-references in an x64 PE: who calls, jumps to or loads an address (or a string).

For research on Mass Effect LE's exe, run it on the disassemblable copy made by
the local-only local-tools/splice_text.py (the shipped exe's code is encrypted). It works on any unencrypted PE too.

    python -I tools/xref.py <pe> --rva 0x4C5300                 # calls/jumps/loads of an RVA
    python -I tools/xref.py <pe> --string "SkelControlLimb"      # references to a string (ASCII or UTF-16)
    python -I tools/xref.py <pe> --natives [--filter Bone]       # UE3 native table: execName -> RVA

It scans code bytes for E8/E9 rel32 and RIP-relative memory operands: lea, mov (all widths,
including byte moves and immediate stores), the ALU ops, test/cmp with immediates, movzx/movsx,
setcc and the common SSE loads/stores/compares, and call/jmp through memory. A byte scan can
occasionally produce a false positive (a misaligned match); each hit is reported with its
containing function (from .pdata) to make that easy to judge. Unusual encodings (VEX/AVX, x87)
aren't decoded, so "0 references" is strong evidence but not proof. --natives pairs each
exec* name string with the nearest lea of code within 32 bytes (after, else before): spot-check
one known native before trusting the list. Local research data only.
"""
import argparse
import bisect
import re
import struct
import sys

# One-byte opcodes with a ModRM operand (RIP-relative when (modrm & 0xC7) == 0x05), and the size
# of the immediate that follows the displacement (needed: RIP-relative targets are relative to
# the end of the whole instruction). None: depends on the operand-size prefix (imm16 / imm32).
ONE_BYTE_OPCODES = {
    0x8D: 0, 0x8B: 0, 0x89: 0, 0x8A: 0, 0x88: 0,                    # lea, mov
    0x03: 0, 0x01: 0, 0x2B: 0, 0x29: 0, 0x33: 0, 0x31: 0, 0x0B: 0, 0x09: 0,
    0x23: 0, 0x21: 0, 0x3B: 0, 0x39: 0, 0x3A: 0, 0x38: 0, 0x85: 0, 0x84: 0,  # alu, cmp, test
    0x80: 1, 0x83: 1, 0xC6: 1, 0x81: None, 0xC7: None,              # alu/mov with immediates
}
# Two-byte (0F xx) opcodes with a ModRM operand and no immediate: SSE moves/compares/arith,
# movzx/movsx, setcc, cmovcc.
TWO_BYTE_OPCODES = set(range(0x10, 0x18)) | {0x28, 0x29, 0x2E, 0x2F, 0x51, 0x54, 0x57, 0x58, 0x59, 0x5A,
    0x5C, 0x5D, 0x5E, 0x5F, 0x6E, 0x6F, 0x7E, 0x7F, 0xB6, 0xB7, 0xBE, 0xBF, 0xD6} | set(range(0x90, 0xA0)) | set(range(0x40, 0x50))


class Pe:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        count = struct.unpack_from('<H', d, pe + 6)[0]
        optional_size = struct.unpack_from('<H', d, pe + 20)[0]
        optional = pe + 24
        if struct.unpack_from('<H', d, optional)[0] != 0x20B:
            sys.exit('not a PE32+ image')
        self.base = struct.unpack_from('<Q', d, optional + 24)[0]
        exception_rva, exception_size = struct.unpack_from('<II', d, optional + 112 + 3 * 8)
        self.sections = []
        for i in range(count):
            entry = optional + optional_size + i * 40
            name = d[entry:entry + 8].rstrip(b'\0').decode(errors='replace')
            vsize, rva, rsize, raw = struct.unpack_from('<IIII', d, entry + 8)
            characteristics = struct.unpack_from('<I', d, entry + 36)[0]
            self.sections.append((name, rva, vsize, raw, rsize, characteristics))
        self.functions = []
        offset = self.to_offset(exception_rva)
        if offset is not None:
            for i in range(exception_size // 12):
                begin, end, _ = struct.unpack_from('<III', d, offset + i * 12)
                self.functions.append((begin, end))
        self.functions.sort()
        self.starts = [f[0] for f in self.functions]

    def to_offset(self, rva):
        for _, va, vsize, raw, rsize, _ in self.sections:
            if va <= rva < va + min(vsize, rsize):
                return raw + rva - va
        return None

    def to_rva(self, offset):
        for _, va, vsize, raw, rsize, _ in self.sections:
            if raw <= offset < raw + min(vsize, rsize):
                return va + offset - raw
        return None

    def section_of(self, rva):
        for name, va, vsize, _, _, _ in self.sections:
            if va <= rva < va + vsize:
                return name
        return None

    def code_sections(self):
        return [s for s in self.sections if s[5] & 0x20000000]  # IMAGE_SCN_MEM_EXECUTE

    def function_of(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and self.functions[i][0] <= rva < self.functions[i][1]:
            return self.functions[i][0]
        return None


def scan_references(pe, wanted):
    """Yields (rva, kind, target) for every reference in code to a target in `wanted`
    (a set of RVAs), or to anything when `wanted` is None."""
    d = pe.data
    for _, va, vsize, raw, rsize, _ in pe.code_sections():
        end = raw + min(vsize, rsize)
        skip_until = -1  # a prefixed hit already covered the bytes up to its opcode
        for at in range(raw, end - 7):
            if at <= skip_until:
                continue
            op = d[at]
            rva = va + at - raw
            if op in (0xE8, 0xE9):
                target = rva + 5 + struct.unpack_from('<i', d, at + 1)[0]
                if wanted is None or target in wanted:
                    yield rva, 'call' if op == 0xE8 else 'jmp', target
                continue
            # [66|F2|F3] [REX] opcode [0F xx] modrm disp32 [imm], RIP-relative when
            # (modrm & 0xC7) == 0x05; the target is relative to the instruction's end.
            body = at
            operand16 = False
            if op in (0x66, 0xF2, 0xF3):
                operand16 = op == 0x66
                body += 1
                op = d[body]
            if 0x40 <= op <= 0x4F:
                body += 1
                op = d[body]
            if body + 7 > end:
                continue
            kind = None
            if op == 0xFF:
                modrm = d[body + 1]
                if modrm in (0x15, 0x25):  # call [rip+x], jmp [rip+x]
                    kind, modrm_at, immediate = ('call*' if modrm == 0x15 else 'jmp*'), body + 1, 0
            elif op == 0x0F:
                if d[body + 1] in TWO_BYTE_OPCODES and (d[body + 2] & 0xC7) == 0x05:
                    kind, modrm_at, immediate = 'load', body + 2, 0
            elif op in (0xF6, 0xF7) and (d[body + 1] & 0xC7) == 0x05 and (d[body + 1] & 0x38) == 0:
                kind, modrm_at, immediate = 'test', body + 1, 1 if op == 0xF6 else (2 if operand16 else 4)  # test r/m, imm
            elif op in ONE_BYTE_OPCODES and (d[body + 1] & 0xC7) == 0x05:
                immediate = ONE_BYTE_OPCODES[op]
                if immediate is None:
                    immediate = 2 if operand16 else 4
                kind = 'lea' if op == 0x8D else 'load'
                modrm_at = body + 1
            if kind is None:
                continue
            next_instruction = modrm_at + 5 + immediate
            target = va + next_instruction - raw + struct.unpack_from('<i', d, modrm_at + 1)[0]
            skip_until = body  # matches starting inside the prefixes are this instruction again
            if wanted is None or target in wanted:
                yield rva, kind, target


def scan_pointers(pe, wanted):
    """Yields (rva, 'ptr', target) for absolute 8-byte pointers to a wanted RVA stored in data
    (vtables, constant-initialized structs, string tables)."""
    d = pe.data
    for target in wanted:
        needle = struct.pack('<Q', pe.base + target)
        for m in re.finditer(re.escape(needle), d):
            rva = pe.to_rva(m.start())
            if rva is not None:
                yield rva, 'ptr', target


def find_strings(pe, text):
    """RVAs of `text` as ASCII and as UTF-16 (each null-terminated)."""
    d = pe.data
    found = []
    for encoded in (text.encode() + b'\0', text.encode('utf-16le') + b'\0\0'):
        for m in re.finditer(re.escape(encoded), d):
            rva = pe.to_rva(m.start())
            if rva is not None:
                found.append(rva)
    return found


def report(pe, references):
    for rva, kind, target in references:
        function = pe.function_of(rva)
        where = f'in 0x{function:X}+0x{rva - function:X}' if function is not None else 'outside any function'
        print(f'  0x{rva:X} {kind:5s} -> 0x{target:X} ({pe.section_of(target)}) {where}')


def natives(pe, name_filter):
    """UE3 native lookup entries: each `TEXT("execName")` string is loaded (lea) right next to
    a lea of its function. Pairs every exec* wide string reference with the nearest following
    lea into code."""
    d = pe.data
    strings = {}
    for m in re.finditer(rb'e\0x\0e\0c\0(?:[A-Za-z0-9_]\0){2,80}\0\0', d):
        rva = pe.to_rva(m.start())
        if rva is not None:
            strings[rva] = m.group().decode('utf-16le').rstrip('\0')
    leas = [(rva, target) for rva, kind, target in scan_references(pe, None) if kind == 'lea']
    leas.sort()
    code = pe.code_sections()

    def in_code(target):
        return any(va <= target < va + vsize for _, va, vsize, _, _, _ in code)

    pairs = []
    for i, (rva, target) in enumerate(leas):
        name = strings.get(target)
        if name is None:
            continue
        # The function's lea usually follows the name's; fall back to the one just before it.
        after = [t for r, t in leas[i + 1:i + 4] if r - rva <= 32 and in_code(t)]
        before = [t for r, t in reversed(leas[max(0, i - 3):i]) if rva - r <= 32 and in_code(t)]
        if after or before:
            pairs.append((name, (after or before)[0]))
    for name, target in sorted(set(pairs)):
        if name_filter is None or name_filter.lower() in name.lower():
            print(f'{name:48s} 0x{target:X}')
    print(f'# {len(set(pairs))} native functions paired ({len(strings)} exec* strings)', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('pe')
    parser.add_argument('--rva', action='append', default=[], help='target RVA (hex), repeatable')
    parser.add_argument('--string', action='append', default=[], help='target string, repeatable')
    parser.add_argument('--natives', action='store_true', help='list the UE3 native function table')
    parser.add_argument('--filter', help='with --natives: only names containing this')
    args = parser.parse_args()
    pe = Pe(args.pe)

    if args.natives:
        natives(pe, args.filter)
        return
    targets = {int(r, 16): f'0x{int(r, 16):X}' for r in args.rva}
    for text in args.string:
        for rva in find_strings(pe, text):
            targets[rva] = f'"{text}" at 0x{rva:X}'
    if not targets:
        parser.error('give --rva, --string or --natives')
    by_target = {}
    references = list(scan_references(pe, set(targets))) + list(scan_pointers(pe, set(targets)))
    for reference in references:
        by_target.setdefault(reference[2], []).append(reference)
    for target, label in targets.items():
        references = by_target.get(target, [])
        print(f'{label}: {len(references)} references')
        report(pe, references)


if __name__ == '__main__':
    main()
