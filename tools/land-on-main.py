# SPDX-License-Identifier: MIT
"""Lands a worktree's commit on main in the main checkout, which may hold another session's uncommitted edits.

  python tools/land-on-main.py <old-main> <new>      # full or short hashes; <new> must descend from <old-main>

main moves by compare-and-swap (refused unless main is still at <old-main>: a peer committed in between, rebase first); files
the main checkout has not touched are checked out; a file with local edits gets the commit's hunks merged three-way into the
working file (refused unless clean); a working file with CRLF endings is merged as LF and written back with CRLF; the index of
each landed file is the commit's, so the other session's edits stay unstaged edits. Never pushes. The main checkout is the
first entry of `git worktree list`. Run it from any worktree of the repository; it touches nothing when it refuses.
"""
import hashlib
import os
import subprocess
import sys
import tempfile


def git(repo, *a, check=True):
    r = subprocess.run(['git', '-C', repo, *a], capture_output=True)
    if check and r.returncode != 0:
        raise SystemExit(f'git {" ".join(a)}: {r.stderr.decode(errors="replace").strip()}')
    return r


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    here = os.getcwd()
    first = git(here, 'worktree', 'list', '--porcelain').stdout.decode().splitlines()[0]
    repo = first.split(' ', 1)[1]
    old = git(repo, 'rev-parse', argv[1]).stdout.decode().strip()
    new = git(repo, 'rev-parse', argv[2]).stdout.decode().strip()
    head = git(repo, 'rev-parse', 'refs/heads/main').stdout.decode().strip()
    if old != head:
        raise SystemExit(f'main is at {head[:8]}, not {old[:8]}: not landing (rebase onto it first)')
    if git(repo, 'merge-base', '--is-ancestor', old, new, check=False).returncode != 0:
        raise SystemExit(f'{new[:8]} does not descend from {old[:8]}: not landing')
    if git(repo, 'diff', '--cached', '--quiet', check=False).returncode != 0:
        raise SystemExit('the main checkout has staged changes: not landing')
    files = git(repo, 'diff', '--name-only', old, new).stdout.decode().split()
    plan = {}
    for f in files:
        path = os.path.join(repo, *f.split('/'))
        dirty = os.path.exists(path) and git(repo, 'diff', '--quiet', '--', f, check=False).returncode != 0
        if not dirty:
            plan[f] = None
            continue
        work = open(path, 'rb').read()
        crlf = b'\r\n' in work
        work_lf = work.replace(b'\r\n', b'\n') if crlf else work
        base = git(repo, 'show', f'{old}:{f}', check=False).stdout
        theirs = git(repo, 'show', f'{new}:{f}').stdout
        d = tempfile.mkdtemp()
        paths = []
        for name, data in (('work', work_lf), ('base', base), ('new', theirs)):
            p = os.path.join(d, name)
            open(p, 'wb').write(data)
            paths.append(p)
        r = subprocess.run(['git', 'merge-file', '-p', *paths], capture_output=True)
        if r.returncode != 0:
            raise SystemExit(f'{f}: the three-way merge with the main checkout\'s own edits is not clean ({r.returncode} conflicts): not landing')
        merged = r.stdout.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n') if crlf else r.stdout
        plan[f] = (hashlib.sha1(work).hexdigest(), merged)
    print('plan:', {f: ('merge' + (' (CRLF)' if v and b'\r\n' in v[1] else '') if v else 'checkout') for f, v in plan.items()})
    git(repo, 'update-ref', 'refs/heads/main', new, head)
    clean = [f for f, v in plan.items() if v is None]
    deleted = [f for f in clean if git(repo, 'cat-file', '-e', f'{new}:{f}', check=False).returncode != 0]
    if deleted:
        git(repo, 'rm', '-q', '--', *deleted)
    present = [f for f in clean if f not in deleted]
    if present:
        git(repo, 'checkout', new, '--', *present)
    for f, v in plan.items():
        if v is None:
            continue
        before, merged = v
        path = os.path.join(repo, *f.split('/'))
        git(repo, 'reset', '-q', new, '--', f)
        if hashlib.sha1(open(path, 'rb').read()).hexdigest() != before:
            print(f'{f}: changed while landing: index updated, working file left as it is (merge the commit\'s hunks by hand)')
            continue
        open(path, 'wb').write(merged)
    print('main ->', git(repo, 'rev-parse', '--short', 'refs/heads/main').stdout.decode().strip())
    status = git(repo, 'status', '--short', '--', *files).stdout.decode()
    print(status or '(the landed files match the commit)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
