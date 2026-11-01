---
type: Gotcha
title: A `cd` in a Bash tool call moves the whole session
description: The Bash tool's working directory persists between calls, so a `cd` inside one command leaves every later relative path, and the app's file pane, pointing somewhere else; use absolute paths, and if it happened, `cd` back to the project root once.
tags: [tooling, claude-code]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-30, 2026-10-31]
---

# Symptom

An environment note "Primary working directory: ... (was H:\ME VR Attempt)"; later commands and file tools resolve
relative paths in the wrong folder; files land in a subdirectory.

# Cause

The tool keeps one shell working directory for the session. `cd "<dir>" && ...` moves it for good, not for the one
command. A `cd` to the project root is harmless; a `cd` into a subfolder (to write several files there) is not.

# Fix in place

Use absolute paths in every Bash call (the rule is the first bullet of CLAUDE.md's Gotchas). If it happened, one
`cd "H:/ME VR Attempt"` puts it back. For several files in one folder, write them with absolute paths or the file
tool rather than `cd`-ing into the folder.

# Citations

[1] [CLAUDE.md, Gotchas](../../../CLAUDE.md): "Don't `cd` in Bash tool calls; it moves the session's working directory."
