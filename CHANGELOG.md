# Changelog

## [Unreleased]
### Added
- M6: USB page. USB selective suspend (AC and battery) is changed through the documented powrprof power-scheme API only, defined in
  `data/tweaks/usb.json` and routed through the same engine as the registry tweaks (a new `POWER` path hive + `PowerRegistry`
  adapter over `IPowerApi`): diff preview, backup of the previous value per power plan, per-item and full restore, rollback, dry-run.
  The `POWER\*\<USB subgroup>` allow-list entry is the only power path the policy accepts. USB polling rates and drivers/INF files
  are never touched (the source scan test forbids the driver/service APIs).
- M6: GPU (NVIDIA) page. Detects NVIDIA GPUs (DXGI vendor ID) and shows the driver version; pages are disabled with a reason on
  other systems. No GPU setting is changed: candidate settings are listed in `docs/gpu-candidates.md` and need approval first.
  "Not verified on real NVIDIA hardware" is stated on the page and in the README.
- M5: automatic Affinity optimization. A pure planner (`planAffinity`) picks the best cores from the detected topology — P-cores
  on hybrid CPUs, the larger-L3 CCD (merging CCX groups up to 6 cores) on multi-CCD AMD, nothing on simple CPUs — within a single
  processor group. Users register games (or pick from running processes); profiles are stored in `affinity.json`.
  A lightweight 2-second poll (a timer that is stopped while the feature is off) applies the plan when a registered game starts and
  restores everything when it exits and when the app exits; a persisted journal restores leftovers after a crash.
- M5 safety: processes are opened only with `PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION`, in one place; protected,
  critical, other-user, other-session, Windows-folder, audio and well-known anti-cheat processes are never touched; PID reuse is
  detected by creation time; affinity is never widened; a process that cannot be opened is skipped and logged. Tests scan the source
  for forbidden APIs. README and `docs/anticheat.md` document the anti-cheat caveat.
- M5: OS access is behind `IProcessApi` (`WinProcessApi` / `FakeProcessApi`); the planner and manager are tested on all 16 sample systems.
- M4: system detection at startup — OS (build, edition, Server/LTSC, native architecture), CPU topology (physical/logical cores,
  P/E cores, SMT, L3 sharing groups for multi-CCD, processor groups), GPU vendor via DXGI vendor ID. Shown in a system card with a
  core-layout map on Home.
- M4: features are greyed out with a tooltip reason when unsupported (e.g. "No NVIDIA GPU was detected", "This Windows build is
  not supported"); startup warning for Windows 7/8, pre-1809 Windows 10, Server, LTSC and ARM64.
- M4: detection is split into a thin OS-API layer (`ISystemProbe`, read-only) and pure analysis functions, tested with mock data for
  Intel hybrid, AMD multi-CCD/CCX, simple, no-SMT, VM, >64-thread, Server, LTSC, ARM64 and old-Windows systems. `--demo --sim <name>`
  renders any sample system.
- M4 fix: placeholder pages showed raw translation keys (wrong key prefix); a test now guards the keys.
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
