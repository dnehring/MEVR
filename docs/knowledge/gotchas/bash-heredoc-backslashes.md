---
type: Gotcha
title: The Bash tool collapses doubled backslashes in a heredoc
description: A script passed inline to python through the Bash tool loses one backslash of every pair, so an edit script that searches for C++ string literals with "\\n" finds nothing (or writes "\n" as a real newline); write the script to a file with the file tool and run that.
tags: [tooling, claude-code]
status: stable
timestamp: 2026-11-04T09:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-04T09:00:00Z }
bitten: [2026-11-04, 2026-11-04, 2026-11-04, 2026-11-04, 2026-11-04, 2026-11-05, 2026-11-05, 2026-11-05]
---

# Symptom

| wording | where |
|---|---|
| an edit script's `assert s.count(old) == 1` fails on a block it plainly contains | inline `python - <<'EOF'` editing HandCalibration.cpp's `"\n"` literals |
| a path built with `'\\'` comes out without its separator | the galaxy disc's offline preview script |
| a C++ `printf("...\n")` an edit script wrote came out split over two lines, the build failing | the menu preview's `--gameart` line |
| `error C2001: newline in constant` right after an edit script added a `std::format("...")` with a leading newline escape | inline `python - <<EOF` adding the power lock's scan note to game/PowerWheel.cpp (2026-11-05) |
| `bash: -c: line 131: unexpected EOF while looking for matching ``'` and nothing edited | an inline `<<'EOF'` edit script whose replacements held C++ format strings with `\\"` (the game pointer's status JSON, 2026-11-05) |

# Cause

The Bash tool's command passes through a layer that turns `\\` into `\` even inside a quoted heredoc (`<<'EOF'`), so
Python receives `"\n"` (a newline escape) where the script said `"\\n"` (a backslash and an n). Searches for C++ source
text containing escapes then never match, and replacements write the wrong bytes.

# Check

`print(repr(old))` in the script: a `\n` where you expected `\\n`.

# Fix in place

For any edit or script with backslashes, write the script with the file tool (Write) into the scratchpad and run it
with `python -I <file>`; build backslashes from `chr(92)` or a `BS = '\\'` constant in that file. Inline heredocs are
fine for text without backslashes.

# Citations

[1] [CLAUDE.md, Gotchas](../../../CLAUDE.md): the short form.
