# SPDX-License-Identifier: MIT
"""Removes the repository's finished worktrees: their work is on main, nothing is uncommitted, nobody used them lately.

  python tools/clean-worktrees.py                 # lists what it would remove and why it keeps the rest (changes nothing)
  python tools/clean-worktrees.py --apply         # removes them
  python tools/clean-worktrees.py --apply --idle-hours 2 --keep left-hand --delete-branches

A worktree is removed only when all of these hold, checked again right before its removal:
  - it isn't the main checkout and isn't locked (`git worktree lock <path>` keeps one for good);
  - `git status --porcelain` is empty: no modified, staged or untracked (non-ignored) file;
  - its HEAD is main or an ancestor of main (land-on-main.py lands the commit itself, so a landed branch counts);
  - nothing at its top level, nor its index or HEAD, changed within --idle-hours (default 12): a session may be in it.
Removing one deletes its folder and everything ignored in it (build directories: 1-6 GB each); its commits stay in git.
A folder another program holds open can survive as empty directories; it goes once that program ends. --delete-branches
also deletes each removed worktree's branch (`git branch -d`, merged only). Never touches main, never pushes.
Worktrees pile up because nothing removes them once their work lands (H: was full on 2026-11-13: 34 of them, 93 GB).
"""
import argparse
import os
import shutil
import stat
import subprocess
import sys
import time


def git(repo, *a, check=True):
    r = subprocess.run(['git', '-C', repo, *a], capture_output=True)
    if check and r.returncode != 0:
        raise SystemExit(f'git {" ".join(a)}: {r.stderr.decode(errors="replace").strip()}')
    return r


def worktrees(repo):
    """The `git worktree list --porcelain` entries as dicts (worktree, HEAD, branch, locked, prunable...)."""
    out, cur = [], {}
    for line in git(repo, 'worktree', 'list', '--porcelain').stdout.decode(errors='replace').splitlines():
        if not line:
            if cur:
                out.append(cur)
            cur = {}
            continue
        key, _, value = line.partition(' ')
        cur[key] = value
    if cur:
        out.append(cur)
    return out


def newest_change(path):
    """The newest modification time among the worktree's top-level entries and its git index and HEAD."""
    times = []
    try:
        with os.scandir(path) as it:
            for e in it:
                try:
                    times.append(e.stat(follow_symlinks=False).st_mtime)
                except OSError:
                    pass
    except OSError:
        pass
    gitdir = git(path, 'rev-parse', '--absolute-git-dir', check=False).stdout.decode().strip()
    for name in ('index', 'HEAD', os.path.join('logs', 'HEAD')):
        try:
            times.append(os.path.getmtime(os.path.join(gitdir, name)))
        except OSError:
            pass
    return max(times) if times else 0.0


def verdict(repo, wt, main, idle_s, keep):
    """None if the worktree may go, else why it stays."""
    path = wt['worktree']
    if any(k.lower() in path.lower() for k in keep):
        return 'kept by --keep'
    if 'locked' in wt:
        return 'locked'
    if 'prunable' in wt or not os.path.isdir(path):
        return 'missing (git worktree prune drops it)'
    status = git(path, 'status', '--porcelain', check=False)
    if status.returncode != 0:
        err = status.stderr.decode(errors='replace').strip().splitlines()
        return 'git status failed: ' + (err[0] if err else f'exit {status.returncode}')
    if status.stdout.strip():
        n = len(status.stdout.decode(errors='replace').splitlines())
        return f'{n} uncommitted change(s)'
    head = wt.get('HEAD', '')
    if git(repo, 'merge-base', '--is-ancestor', head, main, check=False).returncode != 0:
        return 'commits not on main'
    age = time.time() - newest_change(path)
    if age < idle_s:
        return f'used {age / 3600:.1f} h ago'
    return None


def force_rmtree(path):
    def retry(func, p, _exc):
        try:
            os.chmod(p, stat.S_IWRITE)
            func(p)
        except OSError:
            pass
    long = path if path.startswith('\\\\?\\') or os.name != 'nt' else '\\\\?\\' + os.path.abspath(path)
    shutil.rmtree(long, onerror=retry)


def free_gb(path):
    try:
        return shutil.disk_usage(path).free / 2**30
    except OSError:
        return float('nan')


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--apply', action='store_true', help='remove them (without it, only list)')
    ap.add_argument('--idle-hours', type=float, default=12.0, help='keep worktrees changed more recently (default 12)')
    ap.add_argument('--keep', action='append', default=[], help='keep worktrees whose path contains this text (repeatable)')
    ap.add_argument('--delete-branches', action='store_true', help="also delete each removed worktree's merged branch")
    args = ap.parse_args(argv[1:])

    here = os.getcwd()
    entries = worktrees(here)
    repo = entries[0]['worktree']  # the main checkout
    main_rev = git(repo, 'rev-parse', '--verify', 'refs/heads/main').stdout.decode().strip()
    idle_s = args.idle_hours * 3600

    go, stay = [], []
    for wt in entries[1:]:
        why = verdict(repo, wt, main_rev, idle_s, args.keep)
        (stay if why else go).append((wt, why))

    for wt, why in stay:
        print(f'keep    {wt["worktree"]}  ({why})')
    for wt, _ in go:
        print(f'{"remove " if args.apply else "would remove"}  {wt["worktree"]}  ({wt.get("branch", "detached").replace("refs/heads/", "")})')
    if not args.apply:
        print(f'\n{len(go)} to remove, {len(stay)} kept. Nothing changed: run with --apply to remove them.')
        git(repo, 'worktree', 'prune', '--dry-run', '-v', check=False)
        return 0

    drives = {os.path.splitdrive(os.path.abspath(wt['worktree']))[0] or '/' for wt, _ in go}
    before = {d: free_gb(d + os.sep) for d in drives}
    removed, leftovers = 0, []
    for wt, _ in go:
        path = wt['worktree']
        why = verdict(repo, wt, main_rev, idle_s, args.keep)  # again: a session may have started in it meanwhile
        if why:
            print(f'skipped {path}  ({why}, changed since the list)')
            continue
        r = git(repo, 'worktree', 'remove', '--force', path, check=False)
        if r.returncode != 0:
            force_rmtree(path)
            git(repo, 'worktree', 'prune', check=False)
        if os.path.isdir(path):
            # Empty directories a program still holds open (a build node's or a shell's working directory) stay behind.
            for root, dirs, _files in os.walk(path, topdown=False):
                for d in dirs:
                    try:
                        os.rmdir(os.path.join(root, d))
                    except OSError:
                        pass
            try:
                os.rmdir(path)
            except OSError:
                leftovers.append(path)
        removed += 1
        branch = wt.get('branch', '').replace('refs/heads/', '')
        if args.delete_branches and branch and branch != 'main':
            b = git(repo, 'branch', '-d', branch, check=False)
            print(f'removed {path}' + (f'; branch {branch} deleted' if b.returncode == 0 else f'; branch {branch} kept'))
        else:
            print(f'removed {path}')
    git(repo, 'worktree', 'prune', check=False)
    print(f'\n{removed} removed, {len(stay)} kept.')
    for d in sorted(drives):
        print(f'{d} free: {before[d]:.1f} GB -> {free_gb(d + os.sep):.1f} GB')
    if leftovers:
        print(f'{len(leftovers)} folder(s) left behind, held open by another program (they hold no files once git is done):')
        for p in leftovers:
            print(f'  {p}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
