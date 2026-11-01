# Changelog

All notable changes are documented here. The daemon and the niri patch series
are versioned together.

## 0.2.0 — 2026-11-01

Public-release polish. No protocol changes; the niri patch series is unchanged.

- `--version`, a `--help` that documents every command, and `--fake` promoted
  from an undocumented debugging flag to a documented demo mode: a sine-wave
  pointer with no patched compositor, running alongside the real daemon.
- Config errors are reported instead of silently clamped: unknown keys,
  non-numeric values, out-of-range values and bad colours are listed by
  `ringworm reload` (and logged at startup).
- Built-in defaults now match the documented ones (`radius = 16`,
  `thickness = 3`, `glow = 6`, `alpha = 70`). Anyone with an explicit config
  sees no change.
- Ring math extracted into `daemon/ring.c` (pure, no Wayland). The README demo
  GIF and hero still are rendered from that same code by
  `make -C scripts demo` — no screen capture, so the media cannot drift from
  the renderer.
- `ringworm status` reports `pos=none` before the first position arrives and
  `feed=fake` in demo mode; the CLI exits non-zero when the daemon replies
  with an error.
- The control socket is created mode `0600`.
- `scripts/Makefile` replaces `Makefile.virtmouse` and builds both helper
  binaries (`virtmouse`, `render-demo`).
- New: GitHub Actions CI (daemon builds warning-free, helpers build, shell
  scripts lint, patch series applies to `v26.04`), `CONTRIBUTING.md`,
  `SECURITY.md`, `CODE_OF_CONDUCT.md`, issue and PR templates.

## 0.1.0 — 2026-07-18

First working release.

- niri patch series (against v26.04): cursor position over IPC —
  `CursorPosition` request, `CursorPositionChanged` event-stream event
  (included in the snapshot), `niri msg cursor-position` subcommand.
- `ringworm` daemon: click-through layer-shell ring on every output,
  live-reloadable config, `toggle/on/off/status/reload/stop` control CLI,
  single-instance enforcement, output hotplug support.
- Liveness redesign: no writes after event-stream subscription; the
  connection is the heartbeat (see ADR 0005).
- The ring starts hidden at boot/login; it appears only when invoked
  (keybind or `ringworm on`). `enabled = yes` in the config restores
  always-on behavior.
- `scripts/virtmouse`: unattended acceptance testing via virtual pointer.
- License split: MIT for the daemon and scripts, GPL-3.0-or-later for the
  niri patch series.
