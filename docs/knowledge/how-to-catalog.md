---
type: Playbook
title: How to catalog a system and audit a port against it
description: ME1's behaviour is written down as numbered contracts per category (me1/); a port is audited contract by contract (audits/), so a sequel is compared with what ME1 does, not with what its code is called.
tags: [process, trilogy, parity]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
---

# Why

The owner, 2026-11-17: the port of ME1's functionality to ME2 and ME3 lost things on the way (where menus sit, the
rifles' zoom, hand gestures registering actions ME1 never took), and lanes rebuilt what existed and met the same
problems again. [The parity ledger](../trilogy-parity-ledger.md) rates each system by its code (shared, adapted,
forked, missing). That does not catch a system whose shared code runs on a sequel with other numbers, other gates or
half its inputs. The catalog writes down what ME1 *does*; an audit rates a port against that.

# Where

| folder | type | one file per | written from |
|---|---|---|---|
| [me1/](me1/index.md) | `System` | category of ME1 behaviour | ME1's code, `CLAUDE.md`, `research/` |
| [audits/me2/](audits/me2/index.md) | `Audit` | the same category, on ME2 | the catalog file, ME2's code paths, `research/le2-*` |

Then a line in each folder's `index.md`, a line in [log.md](log.md), and `python tools/check-okf.py docs/knowledge`.

# A catalog file (`type: System`)

Frontmatter: `type: System`, `title`, `description`, `tags`, `status`, `timestamp`, `generated`, and `game: LE1`.

| heading | what goes there |
|---|---|
| `# What the player gets` | five to fifteen lines, the player's words: what they do, what they see |
| `# Contracts` | the table below: every behaviour another game must reproduce, one row each |
| `# Arbitration` | where two features want the same input (a grip squeeze, B, the left trigger, a stick click): who claims it, in which order, and what the loser sees. Leave out only if nothing competes |
| `# Problems ME1 already solved` | each problem this system met and how it was solved, with the gotcha or research section: the list a port must not rediscover |
| `# Code map` | core (game-independent rules), game module, host module, seams, HostServices version; which parts are `MEVR_GAME_LE1`-only |
| `# Settings and defaults` | section, key, shipped default, who reads it (host or mod), whether the menu has a row |
| `# Checks` | log prefixes, snapshot keys, console commands, unit tests, the offline preview |
| `# Citations` | relative links to research notes, gotchas, source files (the linter checks them) |

A contract row:

| column | content |
|---|---|
| id | `<PREFIX>-<nn>`, stable: an audit cites it (`WPN-07`). Never renumber; retire with "retired" |
| behaviour | trigger and effect in one or two sentences, the player's view first |
| numbers | every distance, angle, time, threshold and default that shapes it (0.30 s hold, 0.70 m wide, 22 degrees) |
| gates | when it applies and when it stands down (game mode, weapon drawn, cover, scope, menu up, Mako) |
| code | `file` `function` for the rule and for the game-side caller |
| verified | `headset`, `fake headset`, `unit test`, `offline`, or `[verify]`, with the date if known |

Write only what the code or a research note shows. A number read from the code is cited by file; one that could not be
found is `[verify]`, never guessed.

# An audit file (`type: Audit`)

Frontmatter: `type: Audit`, `title`, `description`, `tags`, `status`, `timestamp`, `generated`, `game: LE2` (or LE3),
`against: me1/<file>.md`, `commit: <main's hash when read>`.

| heading | what goes there |
|---|---|
| `# Verdict` | five lines: how much of the category ME2 gets from shared code, and the three findings that matter most |
| `# Contract by contract` | one row per catalog id: rating, how ME2 gets it, where it differs, evidence (`file:line`) |
| `# Reinvented` | code ME2 has that does what a shared or ME1 module already did: both files, what differs, what unifying takes |
| `# Rediscovered problems` | problems ME2's notes record that the catalog's "already solved" list held: both sources, the cost |
| `# Forced by the game` | differences ME2's engine really forces (another API, a missing mechanic), with the evidence |
| `# Not forced` | differences nothing forces: other defaults, other geometry, other gates, an input claimed differently |
| `# What to do` | ordered, each with files and the check that closes it. No code is changed by an audit |

Ratings for a contract:

| rating | meaning |
|---|---|
| same | ME1's code path runs with ME1's numbers and gates |
| same core, other inputs | the shared rule runs, but ME2 feeds it other numbers, frames, gates or defaults |
| adapter | shared core plus a per-game adapter that only translates names and calls |
| reinvented | ME2 has its own implementation of what the shared code does |
| partial | some of the contract's behaviour, the rest absent |
| missing | not on ME2 |
| n/a | ME2 has no such mechanic; the row says what it has instead |
| unknown | the code could not settle it; the row says what run would |

An audit reads code and notes; it runs no game and changes no code. A claim that rests on reading alone says so.
