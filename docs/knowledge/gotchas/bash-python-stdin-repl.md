---
type: Gotcha
title: Python reading stdin from the Bash tool opens its REPL and hangs the call
description: "`python -` with an empty heredoc, or any python given `< /dev/null` (Windows' NUL device) as stdin, decides it is interactive, starts the REPL and loops on console errors until the call times out; run scripts from files and never give python stdin."
tags: [tooling, claude-code]
status: stable
timestamp: 2026-11-04T22:40:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-04T22:40:00Z }
bitten: [2026-11-04, 2026-11-04, 2026-11-05, 2026-11-05]
---

# Symptom

| wording | where |
|---|---|
| a Bash call that should take a second runs into the 120 s timeout, its output a wall of `_pyrepl` tracebacks ending in `OSError: [WinError 123]` | a stray `python - "$S" <<'EOF'` with nothing before `EOF`, in a chain of edit steps |
| the same timeout with no output at all | `python -I - file < /dev/null` |

# Cause

In Git Bash `/dev/null` is Windows' `NUL`, a character device, so Python's `isatty()` on it is true and `python -` (or
a script that reads stdin) starts the interactive REPL. The Bash tool has no console, so the REPL's console setup fails
with `WinError 123` and retries for ever. The rest of a `&&` chain never runs; steps after a `;` or newline wait behind it.

# Check

The task's output file holds `_pyrepl` frames, or `ps` shows a `python` with no script argument.

# Fix in place

Write every script to a file (the Write tool, in the scratchpad) and run `python -I <file> <args>` with no stdin
redirect and no `-`. Stop a hung call with TaskStop, then check which steps of its chain ran before running them again.

# Citations

[1] [The heredoc backslash gotcha](bash-heredoc-backslashes.md): the other reason inline scripts go to files.
