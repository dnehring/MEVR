"""Cross-references in an x64 PE: who calls, jumps to or loads an address (or a string).

For research on Mass Effect LE's exe, run it on the disassemblable copy made by
tools/splice_text.py (the shipped exe's code is encrypted). It works on any unencrypted PE too.

    python -I tools/xref.py <pe> --rva 0x4C5300                 # calls/jumps/loads of an RVA
    python -I tools/xref.py <pe> --string "SkelControlLimb"      # references to a string (ASCII or UTF-16)
    python -I tools/xref.py <pe> --natives [--filter Bone]       # UE3 native table: execName -> RVA

It scans code bytes for E8/E9 rel32 and RIP-relative operands (lea/mov/call/jmp), so a hit can
occasionally be a false positive from a misaligned scan; each one is reported with its
containing function (from .pdata) to make that easy to judge. Local research data only.
"""
import argparse
import bisect
import re
import struct
import sys

# Opcodes whose ModRM operand may be RIP-relative and that take no immediate: lea, mov (both
# directions), cmp, add, sub, xor, or, and, test.
RIP_OPCODES = {0x8D, 0x8B, 0x89, 0x3B, 0x39, 0x03, 0x2B, 0x33, 0x0B, 0x23, 0x85}


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
        rex_end = -1  # a REX-prefixed hit already covered the byte after its prefix
        for at in range(raw, end - 7):
            if at == rex_end:
                continue
            op = d[at]
            rva = va + at - raw
            if op in (0xE8, 0xE9):
                target = rva + 5 + struct.unpack_from('<i', d, at + 1)[0]
                if wanted is None or target in wanted:
                    yield rva, 'call' if op == 0xE8 else 'jmp', target
                continue
            # [REX] opcode modrm disp32, RIP-relative when (modrm & 0xC7) == 0x05.
            body = at
            if 0x40 <= op <= 0x4F:
                body = at + 1
                op = d[body]
            if op == 0xFF:
                modrm = d[body + 1]
                if modrm in (0x15, 0x25):  # call [rip+x], jmp [rip+x]
                    target = va + (body + 6) - raw + struct.unpack_from('<i', d, body + 2)[0]
                    if body != at:
                        rex_end = body
                    if wanted is None or target in wanted:
                        yield rva, 'call*' if modrm == 0x15 else 'jmp*', target
                continue
            if op in RIP_OPCODES and (d[body + 1] & 0xC7) == 0x05:
                target = va + (body + 6) - raw + struct.unpack_from('<i', d, body + 2)[0]
                if body != at:
                    rex_end = body
                if wanted is None or target in wanted:
                    yield rva, 'lea' if op == 0x8D else 'load', target


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
        for later_rva, later_target in leas[i + 1:i + 4]:
            if later_rva - rva > 32:
                break
            if in_code(later_target):
                pairs.append((name, later_target))
                break
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
