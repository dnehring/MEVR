# SPDX-License-Identifier: MIT
"""Resolve RVAs in our built image and matching PDB using the compiler's offline symbolizer."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('image', type=Path)
p.add_argument('rva', nargs='+', type=lambda v: int(v, 0))
p.add_argument('--symbolizer', type=Path)
args = p.parse_args()
exe = args.symbolizer or shutil.which('llvm-symbolizer')
if not exe:
    root = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio'
    found = sorted(root.glob('*/BuildTools/VC/Tools/MSVC/*/bin/Hostx64/x64/llvm-symbolizer.exe'))
    if found:
        exe = found[-1]
if not exe:
    p.error('Pass --symbolizer with the Visual Studio/LLVM llvm-symbolizer.exe path')
subprocess.run([str(exe), '--obj=' + str(args.image.resolve()), '--relative-address', '--output-style=JSON',
                *[hex(v) for v in args.rva]], check=True)
