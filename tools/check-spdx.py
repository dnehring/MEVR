# SPDX-License-Identifier: MIT
"""Lists our source files that have no SPDX licence identifier, or one we don't use.

    python tools/check-spdx.py                     files without an identifier (exit code 1 if any)
    python tools/check-spdx.py --summary           also the count per licence, and the GPL files
    python tools/check-spdx.py --add MIT FILE...   puts the identifier line at the top of each FILE

Every file we own carries `SPDX-License-Identifier: <licence>` in its first lines, in its own comment
syntax: MIT for our original code, GPL-3.0-only for Halcyon's code (src/halcyon) and the files adapted from
Overlord (NOTICE.md lists them). Third-party code (third_party/, external/) keeps its own licence and isn't
checked; nor are docs (Markdown) and other data files, which are MIT unless marked (README.md, Licensing).

Checked: tracked files plus new files git doesn't ignore, so run it before committing. --add keeps the
file's encoding (a UTF-8 BOM stays first) and its line endings (CRLF or LF), and goes below a shebang,
a Python coding line or an XML declaration.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ALLOWED = ('MIT', 'GPL-3.0-only')

# Third-party code and data with their own licences, and the licence texts themselves.
SKIP_PREFIXES = ('third_party/', 'external/', 'LICENSES/', 'reference/', 'mods/')

# The comment syntax per file type: (start, end). Types not listed here aren't checked.
COMMENT = {
    '.c': ('// ', ''), '.cc': ('// ', ''), '.cpp': ('// ', ''), '.cxx': ('// ', ''),
    '.h': ('// ', ''), '.hh': ('// ', ''), '.hpp': ('// ', ''), '.hxx': ('// ', ''), '.inl': ('// ', ''),
    '.hlsl': ('// ', ''), '.hlsli': ('// ', ''), '.fx': ('// ', ''),
    '.java': ('// ', ''), '.rc': ('// ', ''), '.js': ('// ', ''),
    '.py': ('# ', ''), '.ps1': ('# ', ''), '.psm1': ('# ', ''), '.cmake': ('# ', ''), '.sh': ('# ', ''),
    '.csv': ('# ', ''),  # our CSVs (tools/ghidra/symbols-le1.csv) skip '#' lines
    '.def': ('; ', ''), '.ini': ('; ', ''),
    '.rcss': ('/* ', ' */'), '.css': ('/* ', ' */'),
    '.rml': ('<!-- ', ' -->'), '.xml': ('<!-- ', ' -->'), '.vcxproj': ('<!-- ', ' -->'), '.html': ('<!-- ', ' -->'),
    '.patch': ('', ''),  # special: in the commit message's notes, after the '---' line (git am ignores it)
}
NAMES = {'CMakeLists.txt': ('# ', '')}

TAG = 'SPDX-License-Identifier:'
TAG_RE = re.compile(r'SPDX-License-Identifier:\s*([A-Za-z0-9.+\-]+(?:\s+(?:OR|AND|WITH)\s+[A-Za-z0-9.+\-]+)*)')
HEAD_LINES = 20


def comment_for(path):
    name = os.path.basename(path)
    if name in NAMES:
        return NAMES[name]
    return COMMENT.get(os.path.splitext(name)[1].lower())


def git_files():
    out = subprocess.run(['git', '-C', ROOT, 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
                         capture_output=True, check=True).stdout
    files = sorted(set(f for f in out.decode('utf-8').split('\0') if f))
    return [f for f in files if not f.startswith(SKIP_PREFIXES) and comment_for(f) is not None
            and os.path.isfile(os.path.join(ROOT, f))]


def read_identifier(path):
    """The identifier in the file's head, or None. A patch is searched up to its first diff."""
    with open(path, 'rb') as fh:
        data = fh.read(256 * 1024)
    text = data.decode('utf-8', errors='replace')
    if path.endswith('.patch'):
        head = text.split('\ndiff --git', 1)[0]
    else:
        head = '\n'.join(text.splitlines()[:HEAD_LINES])
    match = TAG_RE.search(head)
    return match.group(1) if match else None


def add_identifier(path, licence):
    syntax = comment_for(path)
    if syntax is None:
        raise SystemExit(f'{path}: unknown comment syntax; add the line by hand')
    with open(path, 'rb') as fh:
        data = fh.read()
    if read_identifier(path):
        return False
    bom = b''
    if data.startswith(b'\xef\xbb\xbf'):
        bom, data = data[:3], data[3:]
    elif data[:2] in (b'\xff\xfe', b'\xfe\xff'):
        raise SystemExit(f'{path}: UTF-16; add the line by hand')
    crlf = data.count(b'\r\n')
    eol = b'\r\n' if crlf and crlf * 2 >= data.count(b'\n') else b'\n'
    lines = data.splitlines(keepends=True)

    if path.endswith('.patch'):
        # After the '---' that ends the commit message: git am drops everything up to the diffstat.
        at = next((i + 1 for i, l in enumerate(lines) if l.rstrip(b'\r\n') == b'---'), None)
        if at is None:
            raise SystemExit(f'{path}: no "---" line; add the line by hand')
        line = f'{TAG} {licence}'.encode() + eol
    else:
        start, end = syntax
        line = f'{start}{TAG} {licence}{end}'.encode() + eol
        at = 0
        if lines and lines[0].startswith(b'#!'):
            at = 1
        if path.endswith('.py') and len(lines) > at and re.match(rb'^[ \t\f]*#.*?coding[:=]', lines[at]):
            at += 1
        if lines and lines[0].lstrip().startswith(b'<?xml'):
            at = 1
        if path.endswith(('.ps1', '.psm1')) and len(lines) > at and lines[at].lstrip().startswith(b'<#'):
            line += eol  # a blank line, or PowerShell takes our line for the script's comment-based help
    if at > 0 and lines and not lines[at - 1].endswith((b'\n', b'\r')):
        lines[at - 1] += eol
    lines.insert(at, line)
    with open(path, 'wb') as fh:
        fh.write(bom + b''.join(lines))
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--summary', action='store_true', help='count the files per licence and list the GPL ones')
    parser.add_argument('--add', metavar='LICENCE', help='add this identifier to FILES (e.g. MIT)')
    parser.add_argument('files', nargs='*', help='files for --add')
    args = parser.parse_args()

    if args.add:
        if args.add not in ALLOWED:
            raise SystemExit(f'unknown licence {args.add!r}: we use {", ".join(ALLOWED)}')
        for f in args.files:
            print(('added   ' if add_identifier(f, args.add) else 'has one ') + f)
        return 0

    missing, unknown, by_licence = [], [], {}
    for f in git_files():
        ident = read_identifier(os.path.join(ROOT, f))
        if ident is None:
            missing.append(f)
        elif ident not in ALLOWED:
            unknown.append((f, ident))
        else:
            by_licence.setdefault(ident, []).append(f)
    for f in missing:
        print(f'no identifier: {f}')
    for f, ident in unknown:
        print(f'not one of ours ({ident}): {f}')
    if args.summary:
        for licence in ALLOWED:
            print(f'{licence}: {len(by_licence.get(licence, []))} files')
        for f in by_licence.get('GPL-3.0-only', []):
            print(f'  GPL-3.0-only  {f}')
    if missing or unknown:
        print(f'{len(missing)} without an identifier, {len(unknown)} with another; see tools/check-spdx.py --help')
        return 1
    if not args.summary:
        print('every source file has an SPDX identifier')
    return 0


if __name__ == '__main__':
    sys.exit(main())
