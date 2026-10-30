# SPDX-License-Identifier: MIT
"""Checks an Open Knowledge Format bundle (https://okf.md/spec/, version 0.2): ours is docs/knowledge.

    python tools/check-okf.py docs/knowledge        prints every problem; exit code 1 if there is one

What it checks (the format's conformance rules, its section 9, plus the conventions in docs/knowledge/how-to-write.md):
  - every .md that isn't index.md or log.md starts with a YAML frontmatter block (`---` on the first line, a `---`
    closing it) whose `type` is non-empty; our `bitten` list holds YYYY-MM-DD dates;
  - index.md has no frontmatter except at the bundle root (okf_version); its body is only headings, blank lines and
    `* [title](link) - description` entries; every entry's link exists; every concept, and every subdirectory that
    holds concepts, in that directory is listed;
  - log.md starts with a `# ` title and every other heading is `## YYYY-MM-DD`;
  - every relative markdown link in any file resolves to a file (external URLs and `#anchors` aren't checked; links
    starting with `/` are bundle-relative, as the format says).
No YAML library: the frontmatter is read as `key: value` lines, which is all ours use.
"""
import os
import re
import sys
from datetime import datetime

RESERVED = ('index.md', 'log.md')
LINK_RE = re.compile(r'\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
INDEX_ENTRY_RE = re.compile(r'^[*-] \[[^\]]+\]\(([^)\s]+)\) - \S')
KEY_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_-]*):\s*(.*)$')
DATE_RE = re.compile(r'^\d{4}-\d{2}-\d{2}$')
EXTERNAL = ('http://', 'https://', 'mailto:')


def read(path):
    with open(path, 'r', encoding='utf-8-sig') as fh:
        return fh.read()


def frontmatter(text):
    """(keys, body, error): keys is None when the file has no frontmatter block."""
    lines = text.split('\n')
    if not lines or lines[0].strip() != '---':
        return None, text, None
    for i in range(1, len(lines)):
        if lines[i].strip() == '---':
            keys = {}
            last = None
            for line in lines[1:i]:
                if not line.strip() or line.lstrip().startswith('#'):
                    continue
                match = KEY_RE.match(line)
                if match:
                    last = match.group(1)
                    keys[last] = match.group(2).strip()
                elif line[:1] in (' ', '\t', '-') and last is not None:
                    keys[last] = (keys[last] + ' ' + line.strip()).strip()
                else:
                    return keys, '\n'.join(lines[i + 1:]), f'frontmatter line not `key: value`: {line.strip()!r}'
            return keys, '\n'.join(lines[i + 1:]), None
    return {}, '', 'frontmatter opened with `---` but never closed'


def body_lines(text):
    """The lines outside fenced code blocks, with their 1-based numbers."""
    fenced = False
    for number, line in enumerate(text.split('\n'), 1):
        if line.lstrip().startswith('```'):
            fenced = not fenced
            continue
        if not fenced:
            yield number, line


def check_links(path, text, bundle, problems):
    base = os.path.dirname(path)
    for number, line in body_lines(text):
        for target in LINK_RE.findall(line):
            if target.startswith(EXTERNAL) or target.startswith('#'):
                continue
            target = target.split('#', 1)[0]
            if not target:
                continue
            resolved = os.path.join(bundle, target.lstrip('/')) if target.startswith('/') else os.path.join(base, target)
            if not os.path.exists(os.path.normpath(resolved)):
                problems.append(f'{rel(path, bundle)}:{number}: broken link {target}')


def check_concept(path, bundle, problems):
    text = read(path)
    keys, _, error = frontmatter(text)
    if keys is None:
        problems.append(f'{rel(path, bundle)}: no frontmatter (a concept starts with `---`)')
        return
    if error:
        problems.append(f'{rel(path, bundle)}: {error}')
    if not keys.get('type'):
        problems.append(f'{rel(path, bundle)}: frontmatter has no non-empty `type` (the one key the format requires)')
    bitten = keys.get('bitten')
    if bitten is not None:
        for date in [d.strip() for d in bitten.strip('[]').split(',') if d.strip()]:
            if not DATE_RE.match(date) or not valid_date(date):
                problems.append(f'{rel(path, bundle)}: `bitten` date {date!r} is not YYYY-MM-DD')
    check_links(path, text, bundle, problems)


def valid_date(text):
    try:
        datetime.strptime(text, '%Y-%m-%d')
        return True
    except ValueError:
        return False


def check_index(path, bundle, problems):
    text = read(path)
    keys, body, error = frontmatter(text)
    is_root = os.path.normpath(os.path.dirname(path)) == os.path.normpath(bundle)
    if keys is not None and not is_root:
        problems.append(f'{rel(path, bundle)}: an index has no frontmatter (only the bundle root\'s may)')
    if error:
        problems.append(f'{rel(path, bundle)}: {error}')
    listed = set()
    for number, line in body_lines(body):
        if not line.strip() or line.startswith('# '):
            continue
        match = INDEX_ENTRY_RE.match(line)
        if match:
            listed.add(os.path.normpath(match.group(1).split('#', 1)[0]))
            continue
        problems.append(f'{rel(path, bundle)}:{number}: an index holds only `# ` headings and '
                        f'`* [title](link) - description` entries: {line.strip()[:60]!r}')
    check_links(path, text, bundle, problems)
    folder = os.path.dirname(path)
    for name in sorted(os.listdir(folder)):
        full = os.path.join(folder, name)
        if os.path.isfile(full) and name.endswith('.md') and name not in RESERVED:
            if os.path.normpath(name) not in listed:
                problems.append(f'{rel(path, bundle)}: {name} is not listed')
        elif os.path.isdir(full) and has_concepts(full):
            if not any(entry in listed for entry in (os.path.normpath(os.path.join(name, 'index.md')), name,
                                                      os.path.normpath(name + '/'))):
                problems.append(f'{rel(path, bundle)}: the {name}/ directory is not listed (link its index.md)')


def has_concepts(folder):
    for root, _, files in os.walk(folder):
        if any(f.endswith('.md') for f in files):
            return True
    return False


def check_log(path, bundle, problems):
    text = read(path)
    seen_title = False
    for number, line in body_lines(text):
        if line.startswith('# ') and not seen_title:
            seen_title = True
            continue
        if line.startswith('#'):
            match = re.match(r'^## (\S+)\s*$', line)
            if not match or not DATE_RE.match(match.group(1)) or not valid_date(match.group(1)):
                problems.append(f'{rel(path, bundle)}:{number}: a log heading is `## YYYY-MM-DD`: {line.strip()!r}')
        elif line.strip() and not seen_title:
            problems.append(f'{rel(path, bundle)}:{number}: the log starts with a `# ` title')
            seen_title = True
    if not seen_title:
        problems.append(f'{rel(path, bundle)}: the log has no `# ` title')
    check_links(path, text, bundle, problems)


def rel(path, bundle):
    return os.path.relpath(path, bundle).replace('\\', '/')


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    bundle = os.path.abspath(argv[1])
    if not os.path.isdir(bundle):
        print(f'not a directory: {bundle}')
        return 2
    problems = []
    count = 0
    for root, dirs, files in os.walk(bundle):
        dirs.sort()
        for name in sorted(files):
            if not name.endswith('.md'):
                continue
            path = os.path.join(root, name)
            count += 1
            if name == 'index.md':
                check_index(path, bundle, problems)
            elif name == 'log.md':
                check_log(path, bundle, problems)
            else:
                check_concept(path, bundle, problems)
    for problem in problems:
        print(problem)
    if problems:
        print(f'{len(problems)} problem(s) in {count} files; see tools/check-okf.py --help')
        return 1
    print(f'OKF bundle {rel(bundle, os.getcwd())}: {count} files, every concept typed, indexes complete, links resolve')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
