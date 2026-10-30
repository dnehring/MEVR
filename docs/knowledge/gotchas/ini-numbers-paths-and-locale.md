---
type: Gotcha
title: "Settings code: one ini reader, no C-locale number functions, no path.string()"
description: "`strtod`, `sscanf` and `%f` follow the C locale (a decimal comma breaks them), `std::filesystem::path::string()` throws for characters outside the ANSI code page, and the host and the mod must read one file with one set of rules."
tags: [settings, ini, portability, code-review]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-26]
---

# Symptom

A setting reads as 0 or its default on a machine with a decimal-comma locale; an exception on a game path with
Cyrillic (or any non-ANSI) characters; the host and the mod disagree about a setting.

# Rules

- Numbers in and out through `ini::Number` / `ini::Fixed` / `std::format` (`std::from_chars` / `to_chars`), never
  `strtod`, `sscanf` or `%f`.
- Paths to text through `ini::Utf8(path)`, never `path.string()`. RmlUi opens files through our UTF-8 file interface
  (`menu/MenuUi`).
- The settings file is `<exe dir>\ASI\<GAME>MEVR.ini` for the host and the mod (`engine/SettingsFile`,
  `MevrBridge::SettingsPath()`), read with `core/Ini` (or Win32's profile API): names case-insensitive, the first
  section and key win, a UTF-8 BOM stripped, UTF-16 decoded and then never appended to with narrow text.
- An unwritable ASI folder: warn once, retry every 10 s; the menu shows "Can't save settings".
- Halcyon's own readers (`vr_config.cpp` `sscanf %lf`, `fprintf %.3f`, `xr_session.cpp` `atof`) still use the C
  locale, harmless while nothing calls `setlocale`: never call `setlocale`.

# Citations

[1] [research/33, section 7, QA fixes (2026-10-26)](../../../research/33-installer.md).
[2] Commit 80bd5de (2026-10-26): one ini reader, one settings path, non-ANSI paths.
