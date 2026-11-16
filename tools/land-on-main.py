# SPDX-License-Identifier: MIT
"""Lands a worktree's commit on main in the main checkout, which may hold another session's uncommitted edits.

  python tools/land-on-main.py <old-main> <new>      # full or short hashes; <new> must descend from <old-main>
  python tools/land-on-main.py --dry-run <old-main> <new>   # every check and the plan, nothing touched

main moves by compare-and-swap (refused unless main is still at <old-main>: a peer committed in between, rebase first); files
the main checkout has not touched are checked out; a file with local edits gets the commit's hunks merged three-way into the
working file (refused unless clean); a working file with CRLF endings is merged as LF and written back with CRLF; the index of
each landed file is the commit's, so the other session's edits stay unstaged edits. Never pushes. The main checkout is the
first entry of `git worktree list`. Run it from any worktree of the repository; it touches nothing when it refuses.

A file the commit deletes is taken out of the index and unlinked (never `git rm` without --cached: once main has moved the
index entry differs from HEAD and git refuses, which left main moved and the checkout half landed, 2026-11-16). Everything
that can be refused is checked before main moves: a deleted file the main checkout has edited, an untracked file where the
commit adds another, a directory in a file's way. A git step that still fails after main moved is rolled back: main back to
<old-main> by compare-and-swap, the landed files' index and working copies back to <old-main>'s (the merged files are
written last, so no local edit is ever lost). Tests: tests/LandOnMainTests.py (ctest mevr-land-on-main), in scratch
repositories only.
"""
import hashlib
import os
import subprocess
import sys
import tempfile
import time


def git(repo, *a, check=True):
    r = subprocess.run(['git', '-C', repo, *a], capture_output=True)
    if check and r.returncode != 0:
        raise SystemExit(f'git {" ".join(a)}: {r.stderr.decode(errors="replace").strip()}')
    return r


class GitFailed(Exception):
    pass


def git_try(repo, *a):
    r = subprocess.run(['git', '-C', repo, *a], capture_output=True)
    if r.returncode != 0:
        raise GitFailed(f'git {" ".join(a)}: {r.stderr.decode(errors="replace").strip()}')
    return r


def in_tree(repo, rev, f):
    return git(repo, 'cat-file', '-e', f'{rev}:{f}', check=False).returncode == 0


def unlink(path):
    for attempt in range(10):  # a file another process has open for a moment refuses the delete
        try:
            if os.path.lexists(path):
                os.remove(path)
            return
        except OSError as e:
            if attempt == 9:
                raise GitFailed(f'could not delete {path}: {e}')
            time.sleep(0.5)


def roll_back(repo, old, new, clean, merges):
    """Puts main, the index and the clean files landed so far back at <old>. The merged files are not written yet when this
    runs, so the main checkout's own edits are as they were; only their index entries go back."""
    print('rolling back: main, the index and the files already landed go back to', old[:8])

    def step(*a):
        r = subprocess.run(['git', '-C', repo, *a], capture_output=True)
        if r.returncode != 0:
            print('  git', ' '.join(a), ':', r.stderr.decode(errors='replace').strip())

    step('update-ref', 'refs/heads/main', old, new)
    for f in clean:
        path = os.path.join(repo, *f.split('/'))
        if in_tree(repo, old, f):
            step('checkout', old, '--', f)
        else:
            step('rm', '-q', '-f', '--cached', '--ignore-unmatch', '--', f)
            try:
                if os.path.lexists(path):
                    os.remove(path)
            except OSError as e:
                print(f'  {f}: could not delete it again ({e})')
    for f in merges:
        if in_tree(repo, old, f):
            step('reset', '-q', old, '--', f)
        else:
            step('rm', '-q', '-f', '--cached', '--ignore-unmatch', '--', f)


def main(argv):
    dry = '--dry-run' in argv
    argv = [a for a in argv if a != '--dry-run']
    if len(argv) != 3:
        print(__doc__)
        return 2
    here = os.getcwd()
    first = git(here, 'worktree', 'list', '--porcelain').stdout.decode().splitlines()[0]
    repo = first.split(' ', 1)[1]
    # Revisions as the worktree it runs in names them (its HEAD is its own branch, not main).
    old = git(here, 'rev-parse', '--verify', argv[1] + '^{commit}').stdout.decode().strip()
    new = git(here, 'rev-parse', '--verify', argv[2] + '^{commit}').stdout.decode().strip()
    if old == new:
        raise SystemExit(f'{argv[2]} is {old[:8]}: nothing to land')
    head = git(repo, 'rev-parse', 'refs/heads/main').stdout.decode().strip()
    if old != head:
        raise SystemExit(f'main is at {head[:8]}, not {old[:8]}: not landing (rebase onto it first)')
    if git(repo, 'merge-base', '--is-ancestor', old, new, check=False).returncode != 0:
        raise SystemExit(f'{new[:8]} does not descend from {old[:8]}: not landing')
    if git(repo, 'diff', '--cached', '--quiet', check=False).returncode != 0:
        raise SystemExit('the main checkout has staged changes: not landing')
    # -z and no rename detection: names with spaces whole, a rename as its deletion and its addition.
    files = [f for f in git(repo, 'diff', '--no-renames', '--name-only', '-z', old, new).stdout.decode().split('\0') if f]
    if not files:
        raise SystemExit(f'{new[:8]} changes no file against {old[:8]}: nothing to land')
    plan = {}
    deleted = []
    for f in files:
        path = os.path.join(repo, *f.split('/'))
        gone = not in_tree(repo, new, f)
        added = not in_tree(repo, old, f)
        dirty = os.path.exists(path) and git(repo, 'diff', '--quiet', '--', f, check=False).returncode != 0
        if os.path.isdir(path) and not os.path.islink(path):
            raise SystemExit(f'{f}: the main checkout has a directory where the commit has a file: not landing')
        if gone:
            # Refused before main moves: the other session's edits to a file this commit deletes have nowhere to go.
            if dirty:
                raise SystemExit(f'{f}: the commit deletes it and the main checkout has edited it: not landing')
            deleted.append(f)
            plan[f] = None
            continue
        if added and os.path.lexists(path) and git(repo, 'ls-files', '--error-unmatch', '--', f, check=False).returncode != 0:
            # An untracked file of another session's where the commit adds one: a checkout would write over it.
            theirs = git(repo, 'show', f'{new}:{f}').stdout
            if open(path, 'rb').read().replace(b'\r\n', b'\n') != theirs.replace(b'\r\n', b'\n'):
                raise SystemExit(f'{f}: the commit adds it and the main checkout has an untracked file of that name with '
                                 'other contents: not landing')
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
    print('plan:', {f: ('delete' if f in deleted else 'merge' + (' (CRLF)' if v and b'\r\n' in v[1] else '') if v else 'checkout')
                    for f, v in plan.items()})
    if dry:
        print(f'dry run: main stays at {head[:8]}, nothing touched')
        return 0
    git(repo, 'update-ref', 'refs/heads/main', new, head)
    clean = [f for f, v in plan.items() if v is None]
    present = [f for f in clean if f not in deleted]
    merges = {f: v for f, v in plan.items() if v is not None}
    try:
        if os.environ.get('MEVR_LAND_TEST_FAIL') == 'after-move':  # the tests' way to see the roll-back
            raise GitFailed('a failure asked for by MEVR_LAND_TEST_FAIL')
        for i in range(0, len(deleted), 100):
            # --cached: with main moved the index entry differs from HEAD, and a plain `git rm` refuses.
            git_try(repo, 'rm', '-q', '-f', '--cached', '--ignore-unmatch', '--', *deleted[i:i + 100])
        for f in deleted:
            unlink(os.path.join(repo, *f.split('/')))
        for i in range(0, len(present), 100):
            git_try(repo, 'checkout', new, '--', *present[i:i + 100])
        if os.environ.get('MEVR_LAND_TEST_FAIL') == 'after-checkout':
            raise GitFailed('a failure asked for by MEVR_LAND_TEST_FAIL')
        if merges:
            # Every merged file's index first: a write that fails below must not leave later files' index at the old main
            # (a commit from the main checkout would then undo the landed hunks; 2026-11-12).
            git_try(repo, 'reset', '-q', new, '--', *merges)
    except GitFailed as e:
        print(e)
        roll_back(repo, old, new, clean, list(merges))
        raise SystemExit('not landed: main is back at ' + git(repo, 'rev-parse', '--short', 'refs/heads/main').stdout.decode().strip())
    unwritten = []
    for f, (before, merged) in merges.items():
        path = os.path.join(repo, *f.split('/'))
        if hashlib.sha1(open(path, 'rb').read()).hexdigest() != before:
            print(f'{f}: changed while landing: index updated, working file left as it is (merge the commit\'s hunks by hand)')
            continue
        for attempt in range(10):  # a file another process has open for a moment refuses the write (EINVAL, EACCES)
            try:
                with open(path, 'wb') as out:
                    out.write(merged)
                break
            except OSError as e:
                if attempt == 9:
                    print(f'{f}: could not write the merged file ({e}): index updated, apply the commit\'s hunks to it by hand')
                    unwritten.append(f)
                time.sleep(0.5)
    print('main ->', git(repo, 'rev-parse', '--short', 'refs/heads/main').stdout.decode().strip())
    status = git(repo, 'status', '--short', '--', *files).stdout.decode()
    print(status or '(the landed files match the commit)')
    return 1 if unwritten else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
