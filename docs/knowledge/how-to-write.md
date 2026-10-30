---
type: Playbook
title: How to add a gotcha
description: When a problem comes back or a fact cost hours, write a concept here with its symptom, cause, check, fix in place and citations; when it bites again, add the date.
tags: [process]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
---

# When

- A problem that was fixed before comes back in another guise. Motion blur came back three times under three
  different reports (judder, shimmer, shifting assets) before the pattern was seen; the third time cost a night.
- A fact that cost hours and that nobody would guess from the code: d3d11 swapping its vtable entries, EA's
  launcher refusing a job object, a console that prints nothing.
- Not for: how a feature works (`CLAUDE.md`, `research/`), a one-off bug (`git log`), plans (`docs/`).

# Where

`docs/knowledge/gotchas/<kebab-case>.md`, named for the mechanism, not the symptom: symptoms change, and the
symptom table inside the file carries every wording. Then a line in [gotchas/index.md](gotchas/index.md), a line in
[log.md](log.md) under today's date, and

```
python tools/check-okf.py docs/knowledge
```

which is also ctest `mevr-okf`: frontmatter with a `type` on every concept, indexes complete, every relative link
resolving, log headings as dates.

# Frontmatter

```yaml
---
type: Gotcha
title: <one line, the mechanism>
description: <one sentence>
tags: [area, ...]
status: stable | draft
timestamp: <ISO 8601, UTC>
generated: { by: <producer>/<version> | human:<id>, at: <ISO 8601, UTC> }
bitten: [YYYY-MM-DD, ...]   # every date it bit; dates are added, never replaced
---
```

`type` is the only key the format requires. `bitten` is ours: the count of entries is the count of times the
problem cost a session.

# Body

| heading | what goes there |
|---|---|
| `# Symptom` | the player's or the log's words, every wording it ever had (a table once there are two) |
| `# Cause` | the mechanism, and why the previous fix did not hold |
| `# Check` | the fastest way to confirm or rule it out: a log line, a console command, a snapshot |
| `# Fix in place` | what guards against it now, and what to do when it comes back anyway |
| `# Citations` | the research note, the commit, the file; relative links, which the linter checks |

# When it bites again

Add the date to `bitten`, the new wording to the symptom table, what was learned to the cause, a line to the log.
Then fix the fix.
