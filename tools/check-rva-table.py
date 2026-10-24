# SPDX-License-Identifier: MIT
"""ctest mevr-rva-table (research/34 stage 4): every raw RVA constant in src/ is an entry of our RVA table.

Looks for `constexpr <type> k<Name>Rva = 0x...;` (and `kAddresses{...}`-style literals are gone since stage 4) in
src/ outside src/halcyon (Halcyon's own code keeps its addresses), and checks each value is an `rva` of an entry in
src/engine/Addresses.hpp (an `inline constexpr std::uint32_t k... = 0x...;` or a literal in a kTable entry). A new
constant someone adds (plugin/RenderLevers is the performance session's file) then fails here until it's listed, so
the exe gate checks it at attach.

    python -I tools/check-rva-table.py <repo root>
"""
import os
import re
import sys


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    table = open(os.path.join(root, 'src', 'engine', 'Addresses.hpp'), encoding='utf-8').read()
    listed = set()
    for m in re.finditer(r'inline constexpr std::uint32_t k\w+ = (0x[0-9A-Fa-f]+);', table):
        listed.add(int(m.group(1), 16))
    for m in re.finditer(r'\{"[^"]*", (0x[0-9A-Fa-f]+), Kind::', table):
        listed.add(int(m.group(1), 16))
    constant = re.compile(r'constexpr\s+[\w:]+\s+(k\w*Rva)\s*=\s*(0x[0-9A-Fa-f]+)\s*;')
    missing = []
    seen = 0
    for directory, dirs, files in os.walk(os.path.join(root, 'src')):
        dirs[:] = [d for d in dirs if d != 'halcyon']
        for name in files:
            if not name.endswith(('.cpp', '.hpp', '.h')):
                continue
            path = os.path.join(directory, name)
            text = open(path, encoding='utf-8', errors='replace').read()
            for m in constant.finditer(text):
                seen += 1
                value = int(m.group(2), 16)
                if value not in listed:
                    missing.append(f'{os.path.relpath(path, root)}: {m.group(1)} = 0x{value:X}')
    if missing:
        print('RVA constants not in src/engine/Addresses.hpp (add an entry, with a pattern from tools/rva-patterns.py):')
        for line in missing:
            print('  ' + line)
        return 1
    print(f'{seen} RVA constants, all in our RVA table ({len(listed)} addresses listed)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
