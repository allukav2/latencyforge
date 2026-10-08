# Changelog

## [Unreleased]
### Added
- M3: Kernel / Timer page connected to real data (6 tweaks from `data/tweaks/kernel.json`): per-card toggle, current → new value,
  state badge, risk, restart-required marker, per-item revert, "apply all", "revert everything".
- M3: diff preview dialog before every apply/revert (applies only after confirmation), Dry-run mode, result dialog with rollback info.
- M3: restart-required tracking (compared against the last Windows boot), recovery prompt after an interrupted operation,
  single-instance guard, first-run wizard (disclaimer consent → restore point suggestion), `DISCLAIMER.md`.
- M3: `--demo` mode (in-memory registry, separate state folder) for UI work without touching the real registry.
- M3: tests for status detection, restart tracking, settings consent flag, timestamp parsing, translation key parity (ja/en),
  and a message for every error code.
- M2 (tests confirmed in CI: 79 unit + 7 HKCU integration, all passing): tweak definition schema validation, registry path
  allow/deny policy (security features can never be written), registry layer (RAII, backup, restore, rollback, idempotent
  restore), persistent change history, logger.
- M1: project scaffold (CMake presets, vcpkg manifest, static CRT), Dear ImGui + DirectX 11 window with WARP fallback,
  dark theme with accent color, sidebar navigation, ja/en string tables, settings persistence, event-driven rendering,
  manifest/version resources, CI.
- M3: data-driven presets (`data/presets.json`, validated against the tweak catalog): Safe (2 items) ⊂ Balanced (4) ⊂ Maximum (5).
  Applying a preset uses the normal diff preview → confirm → apply flow; medium-risk items are highlighted and listed first.
  `kernel.wer_user_reporting` is intentionally in no preset (individual toggle only). The first-run wizard suggests "Safe".
### Notes
- Preset descriptions state that effects are build-dependent; "Maximum" means "most items", not "fastest".
