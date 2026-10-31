# Changelog

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
