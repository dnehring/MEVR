# Halcyon's MELE VR code

GPL-3.0, Copyright (C) 2026 Halcyon ([LICENSE](LICENSE)). Imported from the public repositories
and modified for this project:

| Game | Upstream | Commit imported | Here |
|------|----------|-----------------|------|
| LE1 | [jrush64/MassEffect1-VR](https://github.com/jrush64/MassEffect1-VR) | `3c5151409b4ec1d0b8d936bf3b14441da3598c33` (2026-09-30) | `le1/` |

`le1/` holds upstream's `src/` plus its `docs/`, `runtime/MELEVR.ini`, `README.md` and `BUILD.md`
(renamed `UPSTREAM_*.md`). Its vendored MinHook and Dear ImGui are in `third_party/`.

The import commit is verbatim; every later commit touching these files is our modification, so
`git log -p -- src/halcyon` is the full record of changes (GPL-3.0 section 5a).
