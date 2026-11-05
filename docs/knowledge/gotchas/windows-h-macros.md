---
type: Gotcha
title: Windows.h defines macros that eat ordinary identifiers
description: Our sources include Windows.h, whose macros (SetProp, far, near, min, max, GetObject...) silently rewrite a function or variable of that name, so the code fails to compile in one build and not another, with errors that point at the wrong thing.
tags: [tooling, build, windows]
status: stable
timestamp: 2026-11-04T09:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-04T09:00:00Z }
bitten: [2026-11-04, 2026-11-04, 2026-11-04, 2026-11-04]
---

# Symptom

| wording | where |
|---|---|
| the monolithic build fails in `game/GalaxyDump` while `build/` compiled: `SetProp` "is not a member" / redefinition | a function named `SetProp` (601a938), caught by the scope session's players' build |
| `error C2513: 'const float': no variable declared before '='`, then `syntax error: '>'`, `illegal else` | `float const far = ...` in core/vr/WorldUi.cpp |
| `error C2513: 'const auto': no variable declared before '='`, then a C4834 "discarding return value" on every later use | `auto const near = [](...)` a test lambda in tests/Main.cpp (the finger touch tests) |
| a declaration named `small` that won't compile (rpcndr.h's `#define small char`), and `far` again | the Mako gunner's tests in tests/Main.cpp (renamed `tiny`, `distant`) |

# Cause

`Windows.h` defines function-like and object-like macros for many plain words: `SetProp` -> `SetPropW`, `far` and
`near` -> nothing (16-bit leftovers), `small` -> `char` (rpcndr.h), `min`/`max` (unless `NOMINMAX`), `GetObject`, `DrawText`, `CreateWindow`... Which
macros a translation unit sees depends on its include order, so a name can compile in one target (the hot-reload mod)
and break in another (the monolithic ASI, which pulls in more headers), or compile into a call to a different
function. The compiler's error then points at the mangled line, not at the name.

# Check

A baffling syntax error on an ordinary declaration, or one target compiling where another doesn't: look up the name
as a macro (`#define <name>` in the Windows SDK headers, or hover it). `far`, `near`, `small`, `SetProp`, `GetObject`,
`min`, `max` are the usual ones.

# Fix in place

Don't name functions or variables after Win32 API functions or the old memory-model words; the code was renamed
(`SetProperty` in 0a1bbd0, `reach` in core/vr/WorldUi.cpp). Build both `build/` and `build-mono/` before a commit that
adds a function to a file including `Windows.h`.

# Citations

[1] [src/game/GalaxyDump.hpp](../../../src/game/GalaxyDump.hpp): `SetProperty` (was `SetProp`, commit 0a1bbd0).
[2] [src/core/vr/WorldUi.cpp](../../../src/core/vr/WorldUi.cpp): `Place`'s `reach` (was `far`).
