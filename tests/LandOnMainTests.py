# SPDX-License-Identifier: MIT
"""tools/land-on-main.py over scratch repositories (a main checkout with a second worktree), never this repository.

Each case builds a repository in a temporary folder, makes a commit in the worktree and lands it on the main checkout, which
may hold another session's uncommitted edits. Run: python tests/LandOnMainTests.py
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile

TOOL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools', 'land-on-main.py')
ENV = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@example.invalid', GIT_COMMITTER_NAME='t',
           GIT_COMMITTER_EMAIL='t@example.invalid', GIT_CONFIG_NOSYSTEM='1')
for k in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE'):
    ENV.pop(k, None)


def git(repo, *a):
    r = subprocess.run(['git', '-C', repo, '-c', 'core.autocrlf=false', *a], capture_output=True, env=ENV)
    if r.returncode != 0:
        raise AssertionError(f'git {a}: {r.stderr.decode(errors="replace")}')
    return r.stdout.decode().strip()


def write(repo, name, text):
    path = os.path.join(repo, *name.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(text.encode())


def read(repo, name):
    with open(os.path.join(repo, *name.split('/')), 'rb') as f:
        return f.read().decode()


class Repo:
    def __init__(self, root):
        self.main = os.path.join(root, 'main')
        self.work = os.path.join(root, 'work')
        os.makedirs(self.main)
        git(self.main, 'init', '-q', '-b', 'main')
        write(self.main, 'keep.txt', 'one\ntwo\nthree\nfour\nfive\n')
        write(self.main, 'gone.txt', 'to be deleted\n')
        write(self.main, 'dir/also gone.txt', 'a name with a space\n')
        write(self.main, 'moved.txt', 'renamed later\n')
        git(self.main, 'add', '-A')
        git(self.main, 'commit', '-q', '-m', 'base')
        self.old = git(self.main, 'rev-parse', 'HEAD')
        git(self.main, 'worktree', 'add', '-q', '-b', 'lane', self.work)

    def commit(self):
        git(self.work, 'add', '-A')
        git(self.work, 'commit', '-q', '-m', 'lane work')
        return git(self.work, 'rev-parse', 'HEAD')

    def land(self, new, *flags, env=None):
        e = dict(ENV, **(env or {}))
        r = subprocess.run([sys.executable, TOOL, *flags, self.old, new], cwd=self.work, capture_output=True, env=e)
        return r.returncode, (r.stdout + r.stderr).decode(errors='replace')

    def head(self):
        return git(self.main, 'rev-parse', 'refs/heads/main')

    def status(self):
        return git(self.main, 'status', '--porcelain')


def case_deletion(r):
    """A commit that deletes, renames, adds and edits: the bug of 2026-11-16 (git rm refused after main had moved)."""
    os.remove(os.path.join(r.work, 'gone.txt'))
    os.remove(os.path.join(r.work, 'dir', 'also gone.txt'))
    git(r.work, 'mv', 'moved.txt', 'moved to.txt')
    write(r.work, 'keep.txt', 'one\ntwo\nthree\nfour\nFIVE\n')
    write(r.work, 'new/added.txt', 'new\n')
    new = r.commit()
    code, out = r.land(new)
    assert code == 0, out
    assert r.head() == new, out
    assert r.status() == '', r.status()
    assert not os.path.exists(os.path.join(r.main, 'gone.txt'))
    assert not os.path.exists(os.path.join(r.main, 'dir', 'also gone.txt'))
    assert not os.path.exists(os.path.join(r.main, 'moved.txt'))
    assert read(r.main, 'moved to.txt') == 'renamed later\n'
    assert read(r.main, 'new/added.txt') == 'new\n'


def case_deletion_beside_local_edits(r):
    """The main checkout's own edit to another file the commit changes is merged and stays an unstaged edit."""
    write(r.main, 'keep.txt', 'ONE\ntwo\nthree\nfour\nfive\n')
    os.remove(os.path.join(r.work, 'gone.txt'))
    write(r.work, 'keep.txt', 'one\ntwo\nthree\nfour\nFIVE\n')
    new = r.commit()
    code, out = r.land(new)
    assert code == 0, out
    assert r.head() == new
    assert read(r.main, 'keep.txt') == 'ONE\ntwo\nthree\nfour\nFIVE\n'
    assert r.status() == 'M keep.txt', repr(r.status())
    assert not os.path.exists(os.path.join(r.main, 'gone.txt'))


def case_deleted_file_edited_in_main(r):
    """Refused before main moves, nothing touched."""
    write(r.main, 'gone.txt', 'another session is working on this\n')
    os.remove(os.path.join(r.work, 'gone.txt'))
    write(r.work, 'keep.txt', 'changed\n')
    new = r.commit()
    code, out = r.land(new)
    assert code != 0 and 'deletes it' in out, out
    assert r.head() == r.old
    assert read(r.main, 'gone.txt') == 'another session is working on this\n'
    assert read(r.main, 'keep.txt') == 'one\ntwo\nthree\nfour\nfive\n'
    assert r.status() == 'M gone.txt', repr(r.status())


def case_untracked_in_the_way(r):
    write(r.main, 'new/added.txt', 'another session\'s untracked file\n')
    write(r.work, 'new/added.txt', 'new\n')
    os.remove(os.path.join(r.work, 'gone.txt'))
    new = r.commit()
    code, out = r.land(new)
    assert code != 0 and 'untracked' in out, out
    assert r.head() == r.old
    assert read(r.main, 'new/added.txt') == 'another session\'s untracked file\n'
    assert os.path.exists(os.path.join(r.main, 'gone.txt'))


def case_conflict(r):
    write(r.main, 'keep.txt', 'one\ntwo\nthree\nfour\nmine\n')
    write(r.work, 'keep.txt', 'one\ntwo\nthree\nfour\ntheirs\n')
    os.remove(os.path.join(r.work, 'gone.txt'))
    new = r.commit()
    code, out = r.land(new)
    assert code != 0 and 'not clean' in out, out
    assert r.head() == r.old
    assert os.path.exists(os.path.join(r.main, 'gone.txt'))
    assert read(r.main, 'keep.txt') == 'one\ntwo\nthree\nfour\nmine\n'


def case_dry_run(r):
    os.remove(os.path.join(r.work, 'gone.txt'))
    write(r.work, 'keep.txt', 'changed\n')
    new = r.commit()
    code, out = r.land(new, '--dry-run')
    assert code == 0 and "'gone.txt': 'delete'" in out and 'dry run' in out, out
    assert r.head() == r.old
    assert r.status() == ''
    assert os.path.exists(os.path.join(r.main, 'gone.txt'))


def roll_back_case(point):
    def case(r):
        """A step that fails after main moved: main, the index and the files go back; local edits untouched."""
        write(r.main, 'keep.txt', 'ONE\ntwo\nthree\nfour\nfive\n')
        os.remove(os.path.join(r.work, 'gone.txt'))
        write(r.work, 'keep.txt', 'one\ntwo\nthree\nfour\nFIVE\n')
        write(r.work, 'moved.txt', 'edited\n')
        write(r.work, 'new/added.txt', 'new\n')
        new = r.commit()
        code, out = r.land(new, env={'MEVR_LAND_TEST_FAIL': point})
        assert code != 0 and 'not landed' in out, out
        assert r.head() == r.old, out
        assert read(r.main, 'gone.txt') == 'to be deleted\n'
        assert read(r.main, 'moved.txt') == 'renamed later\n'
        assert read(r.main, 'keep.txt') == 'ONE\ntwo\nthree\nfour\nfive\n'
        assert not os.path.exists(os.path.join(r.main, 'new', 'added.txt'))
        assert r.status() == 'M keep.txt', repr(r.status())
        code, out = r.land(new)  # and the same commit lands afterwards
        assert code == 0 and r.head() == new, out
        assert r.status() == 'M keep.txt', repr(r.status())
    case.__name__ = 'case_roll_back_' + point.replace('-', '_')
    return case


def on_rm_error(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)


def main():
    cases = [case_deletion, case_deletion_beside_local_edits, case_deleted_file_edited_in_main, case_untracked_in_the_way,
             case_conflict, case_dry_run, roll_back_case('after-move'), roll_back_case('after-checkout')]
    failed = 0
    for case in cases:
        root = tempfile.mkdtemp(prefix='mevr-land-')
        try:
            case(Repo(root))
            print('ok  ', case.__name__)
        except AssertionError as e:
            failed += 1
            print('FAIL', case.__name__, '\n', e)
        finally:
            shutil.rmtree(root, onerror=on_rm_error)
    print(f'{len(cases) - failed} of {len(cases)} passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
