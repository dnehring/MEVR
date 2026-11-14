---
name: fake-headset-run
description: Runs LE1 unattended on SteamVR's null-driver "fake headset" and puts the machine back after - deploy a build, back up the saves and inis, launch through Steam, wait for a player, drive the game through the console request file, play scenes from validation saves, collect logs, snapshots and records, restore everything - then lands the commit on main from a worktree and installs a clean build. Use when a change must be checked in the running game without the owner, when a scene, level or save has to be played by script, or before handing the owner a headset test.
---

# Running the game unattended (LE1, fake headset)

One command does the whole run and always puts the machine back: **`tools/fake-headset-session.ps1`**. Write what the run does
in a small script; the session tool deploys, backs up, launches, dot-sources your script, and restores in a `finally`.

```powershell
tools\fake-headset-session.ps1 -Tag heads -Run C:\...\scratchpad\run-heads.ps1 -IgnoreSlot -Audit
tools\fake-headset-session.ps1 -Tag one -Command "tools\conversation-from-save.ps1 -All -Career John02 -Only 85 -Stances"
tools\fake-headset-session.ps1 -Restore %LOCALAPPDATA%\MEVR\sessions\<stamp>-<tag>     # after a run that was killed
```

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
| The left stick without controllers | `mevr convo walk <x> <y> [s]` (runner `-Walk N`, `-WanderAt N`); `mevr convo goto [x y]` (the simulated stick to a place) |
| The right stick's turn | `mevr convo turn <deg>` (runner `-TurnAt N`: 90, 90, -180) |
| Pick a reply / skip / end | `mevr convo pick <0..5>` (only with the wheel up), `skip`, `end` |
| What the eyes show | `tools\snapshot.ps1` (PNGs and JSON in `%LOCALAPPDATA%\MEVR\snapshots`; Read the PNGs) |
| A recorded session with controllers | `tools\replay.ps1 -Path f -Loop -Inputs` (the fake headset's controllers are not tracked otherwise) |
| Where every script call goes | `mevr calltrace 10 <words>` |

Validation saves and the scenes they start: `%LOCALAPPDATA%\MEVR\save-dictionary.md` (`tools/save-dictionary.py`); John02's list
for `-All` is `%LOCALAPPDATA%\MEVR\save-dictionary-list-John02.json` with `-FirstSlot 80`; the squad saves (Garrus and Tali on the
Citadel) are `-Career John00 -List %LOCALAPPDATA%\MEVR\squad-saves\list.json -SaveDir %LOCALAPPDATA%\MEVR\squad-saves`.

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
- Host and mod deploy together (one ASI in the players' build); one session deploys at a time.

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
