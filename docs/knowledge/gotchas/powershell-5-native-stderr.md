---
type: Gotcha
title: Windows PowerShell 5.1 turns a native command's stderr into errors
description: Under `$ErrorActionPreference = 'Stop'`, or with `2>&1`, git progress and other stderr output of native exes become terminating errors or NativeCommandError records even when the exe exits 0; check `$LASTEXITCODE` instead.
tags: [tooling, powershell]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
---

# Symptom

A script dies on `git fetch` or another native command that wrote progress to stderr, or `$?` is false although the
exe returned 0.

# Fix

Check `$LASTEXITCODE`, not `$?`; do not redirect a native command's stderr with `2>&1`. `&&` and `||` do not exist in
5.1: `A; if ($?) { B }`. msbuild is not on PATH: use a VS developer shell or CMake.

# Citations

[1] [CLAUDE.md, Gotchas](../../../CLAUDE.md): where the rule was first recorded.
