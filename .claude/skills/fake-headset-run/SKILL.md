---
name: fake-headset-run
description: Runs LE1 unattended on SteamVR's null-driver "fake headset" and puts the machine back after - deploy a build, back up the saves and inis, launch through Steam, wait for a player, drive the game through the console request file, play scenes from validation saves, give it controllers with scripted recordings (hands, sticks, walks, menu clicks), collect logs, snapshots and records, restore everything - then lands the commit on main from a worktree and installs a clean build. Use when a change must be checked in the running game without the owner, when a scene, level or save has to be played by script, when a test needs tracked hands or stick input, or before handing the owner a headset test.
---

# Running the game unattended (LE1, fake headset)

One command does the whole run and always puts the machine back: **`tools/fake-headset-session.ps1`**. Write what the run does
in a small script; the session tool deploys, backs up, launches, dot-sources your script, and restores in a `finally`.

```powershell
tools\fake-headset-session.ps1 -Tag heads -Run C:\...\scratchpad\run-heads.ps1 -IgnoreSlot -Audit
tools\fake-headset-session.ps1 -Tag one -Command "tools\conversation-from-save.ps1 -All -Career John02 -Only 85 -Stances"
tools\fake-headset-session.ps1 -Restore %LOCALAPPDATA%\MEVR\sessions\<stamp>-<tag>     # after a run that was killed
```

**Build `build-mono` first** (`cmake --build build-mono --config Release --target MEVR-LE1`): the session deploys the players'
build, not `build`, and the run prints `the ASI in the game: 0.2.0+<hash>`; check it is the one you just built (2026-11-13: a
run played the old mod after only `build` was rebuilt, and every new number read 0).

What it does, in order: `deploy-asi.ps1 -Game LE1 -BuildDir build-mono` (skip with `-NoDeploy`; `-IgnoreSlot` when the game slot
is held, see below); refuses the players' build without `[Debug] requests = 1` in `ASI\LE1MEVR.ini` (it reads no console
requests otherwise); copies the ME1 saves and `ASI\*.ini` into `%LOCALAPPDATA%\MEVR\sessions\<stamp>-<tag>\backup`;
`fake-headset.ps1 -On`; `launch-game.ps1 -Steam`; waits for `game mode 0|1` (180 s); runs your script with `$Session` and
`$Began` set (and `Say "text"` for timestamped lines); then quits the game (console, then by force after 40 s), turns the fake
headset off, restores every changed save, **moves the saves the run made aside** into the session folder (never deletes),
restores changed inis, keeps `LE1MEVR.log` there, prints the installed ASI's version and, with `-Audit`, the heads audit.

## Running it from Claude Code

- Run it with `run_in_background` (a pass of ten scenes is 15-20 minutes) and arm a `Monitor` on the output file that emits
  only the lines worth acting on and exits at the end, for example
  `grep -E "^=== slot|^#####|did not start|run stopped|done:"` in a loop that breaks on `done:`. Don't poll the file.
- While it runs, keep working: builds of `build` and `build-trilogy` and the tests don't touch the deployed game.
- Your run script is ordinary PowerShell dot-sourced from the repository root. Write it with the Write tool into the scratchpad
  (an inline heredoc loses backslashes). Use `tools\console.ps1 "<command>"` for anything in the game (it waits for
  `console.result`), `tools\conversation-from-save.ps1` to play a scene from a save, `tools\snapshot.ps1 -Delay s -Tag x` for what
  the eyes see.

## What drives the game

| Need | How |
|---|---|
| Any console command, mod verbs included (`mevr ...`) | `tools\console.ps1 "cmd" "cmd2"` |
| A scene from a validation save, judged | `tools\conversation-from-save.ps1 -All -Career John02 -List <list.json> -FirstSlot 80 -Only 85,89` (or `-Save <path> -Action <seq action path> -Class <class>` for one) |
| A controller's stick, as a player holds it | `mevr convo stick left <x> <y> [s]` / `stick right <x> [s]` / `stick off`: the value the conversation's walk and turn read, through the rule that the stick is the wheel's until let go. Runner: `-HoldThroughPick` (the left stick held through a conversation's first pick: it must not walk), `-StickTurnAt N` (the right stick 1.5 s: it must turn), `-WanderAt N` (walk off 3 s) |
| The stick without the wheel's rule | `mevr convo walk <x> <y> [s]` (runner `-Walk N`; it goes round the latch, so it can't test it); `mevr convo goto [x y]` (the simulated stick to a place) |
| A turn by a number | `mevr convo turn <deg>` (runner `-TurnAt N`: 90, 90, -180) |
| Pick a reply / skip / end | `mevr convo pick <0..5>` (only with the wheel up), `skip`, `end` (the world's own early end) |
| A fixed bug brought back (broken runs) | `mevr convo bodyaway off`, `showearly off`, `hide body\|person on`, `nearplane off`, `sticklatch off`, `stickturn off`, `matineefade off` (put each back after its scene; the set's `canaries`, skill `first-person-scene-audit`) |
| What the eyes show | `tools\snapshot.ps1` (PNGs and JSON in `%LOCALAPPDATA%\MEVR\snapshots`; Read the PNGs) |
| A recorded session with controllers | `tools\replay.ps1 -Path f -Loop -Inputs` (the fake headset's controllers are not tracked otherwise) |
| Where every script call goes | `mevr calltrace 10 <words>` |

Validation saves and the scenes they start: `%LOCALAPPDATA%\MEVR\save-dictionary.md` (`tools/save-dictionary.py`); John02's list
for `-All` is `%LOCALAPPDATA%\MEVR\save-dictionary-list-John02.json` with `-FirstSlot 80`; the squad saves (Garrus and Tali on the
Citadel) are `-Career John00 -List %LOCALAPPDATA%\MEVR\squad-saves\list.json -SaveDir %LOCALAPPDATA%\MEVR\squad-saves`. A save for a
scene that has none: `tools/le1-save.py` (read, `write` a place and plot values, `diff --rewind-out`), or `conversation-sweep.py
saves` (CLAUDE.md, conversations in the world).

## Making controller input (the fake headset has none)

Without a replayed recording the fake headset has no controllers: arm IK and the body follower never run and the game sees no
stick. Make a recording with a script, replay it with `tools\replay.ps1 -Path <file> -Loop -Inputs` (`-Inputs`: its buttons and
sticks too; `-Stop` ends it), or give it to the runner (`conversation-from-save.ps1 -Replay <file>`). Write them under
`%LOCALAPPDATA%\MEVR` or the scratchpad, never in the repository.

| Recording | Command |
|---|---|
| A player standing or walking with tracked hands | `python tools/make-walk-recording.py out.mevrrec --plan stand:600 --hands view` (`forward:14,stand:300`: walks 14 s first; `--hands view` holds them up where every snapshot sees them; `--grip` holds a drawn gun; `--pitch` looks down). The audit set's `hands` are these. |
| The head turning a full circle | `python tools/make-look-recording.py out.mevrrec [seconds] [hz] [--sweep YAW] [--pitch DEG]` (frame timing in every direction) |
| Clicks on our menu | `python tools/make-menu-recording.py out.mevrrec "point 150 452; click; scroll 4; hold 4"` (open the menu with its key first) |
| A wrist tap and the omni-tool's rail | `python tools/make-wrist-recording.py out.mevrrec [seconds] [taps] ...` (its docstring lists every field) |
| Driving the Mako | `python tools/make-drive-recording.py out.mevrrec [seconds] [hz] [--press-b \| --controls]` |
| A galaxy-map pick | `python tools/make-galaxy-pick-recording.py <dump.json> <label> out.mevrrec` (or `--snapshot <snapshot.json>`) |
| A pistol's vent rack | `python tools/make-rack-recording.py out.mevrrec [seconds] [period]` |

`python tools/mevrrec.py <file>` summarizes one. A replay with controller poses moves the real cursor when a screen falls back to
the flat panel: set `[Pointer] setCursor = 0` for such runs.

## Other runners

- `tools\conversation-launch.ps1 <name> [-World on] [-Party] [-Snapshot]`: any conversation without a save (opens its level,
  streams its chunk, stands Shepard before the owner, fires it); `-List <text>` searches the catalogue. It resets the plot.
- `tools\conversation-audit.ps1 [-Only a,b] [-Canaries]`: the regression set and the broken runs, each through this session tool
  (skill `first-person-scene-audit`); `conversation-sweep.ps1 -Plan <plan>`: a sampled pass over many scenes.
- `tools\mako-start.ps1 [-Map BIOA_UNC21]`: into the Mako without a save.
- `tools\power-arena.ps1`: soldiers to fight at UNC20. Alone it launches the game itself (run it outside this session tool, with
  the game closed); `-Here` when the game already stands at UNC20. It autosaves into the career: `-Restore` once the game is closed.
- The cafeteria QA has its own runner and README (`tools/qa-cafeteria`).

## What the fake headset can't tell you

- **Frame timing.** It runs at about 31 fps (every capture since 2026-11-09; the owner, 2026-11-13: likely the monitor asleep
  while nobody is at the machine; not to be debugged yet), so a stall under about 20 ms hides in its wait. The frame-timing
  tools (`tools\perf-probe.ps1 -Seconds N -Tag x` then `python tools/perf-probe.py --latest`; `perf-ab.ps1`, `measure-levers.ps1`,
  `spot-survey.ps1`; CLAUDE.md "Frame timing") still show which thread or the GPU does the work; a hitch the owner feels needs a
  capture in the headset (`notes/headset-checklist.md`).
- Comfort, feel and legibility: those are the owner's. Put a line in `notes/headset-checklist.md` for each.

## Reading what happened

- The run's output: the runner prints per scene the session judge's findings (`tools/conversation-session.py`: flash, dip,
  body-hidden, screen, pivot...) and the sheets it drew.
- `<session>\LE1MEVR.log`: grep `convo:`, `moment:`, `eyes:`, `steer:`, `squad:`, `circle:` for the mod's account.
- `python tools/conversation-stance.py --audit --since <Began>` / `--heads` / the sheets (skill `first-person-scene-audit`).
- `crash-breadcrumbs.txt` / `crash-firstchance.txt` in `%LOCALAPPDATA%\MEVR` if the game vanished.

## Rules that bit before

- **Saves and inis are backed up and restored by the tool**; if you ever run the game another way, back them up yourself
  (the game autosaves into the owner's career on its own).
- **Never `tools/game-slot.ps1 return` without asking the owner**: it rolls the game folder back to the take, the owner's own
  play since included. The slot is a lock; deploying over it needs `-IgnoreSlot`, which is fine for these runs.
- **Never `streamlevelin`** (it froze the game); stream chunks with `mevr stream state <name> on` or the runner's `-Chunk`.
- Never start the game from the harness shell by its exe (the job object breaks the EA app): `launch-game.ps1 -Steam` only. A
  dozen restarts in a row can stall the EA app: quit `EADesktop.exe` and launch again.
- "nothing came of the action in 25 s (a plot check on the way?)": the save's plot doesn't let that scene start; pick another
  save or set the plot with `tools/le1-save.py write`.
- Host and mod deploy together (one ASI in the players' build); one session deploys at a time, and one game run at a time across
  sessions: check `Get-Process MassEffect1` and the other sessions before you start.
- Leave the owner's settings as you found them: `[Thespian] on=1` in `ASI\LE1MEVR.ini` is the owner's; a run that turns a setting
  off for itself puts it back.

## After the run: land and install

1. All three builds and the tests: `cmake --build build --config Release --target MEVR-LE1 MEVR-LE1-mod mevr-tests`,
   `cmake --build build-mono --config Release --target MEVR-LE1`, `cmake --build build-trilogy --config Release`,
   `ctest -C Release --test-dir build`, `python tools/check-spdx.py`, `python tools/check-okf.py docs/knowledge`.
2. Commit in the worktree (attribution line from the session), then
   `python tools/land-on-main.py <main's hash> <your commit>`: main moves by compare-and-swap, files the main checkout has not
   touched are checked out, files another session is editing there get your hunks merged three-way (refused unless clean).
   Never push (the owner does).
3. Rebuild `build-mono` at the commit and, with the game closed, `tools\deploy-asi.ps1 -Game LE1 -BuildDir build-mono
   -IgnoreSlot`: the installed version reads `<version>+<hash>` without `-dirty`.

The working copy's text files are CRLF (autocrlf); the Edit tool and Python scripts that keep the file's line endings are
safe; `grep -c $'\r'` in Git Bash is not a reliable check, `git ls-files --eol <file>` is.
