# Plan

Status: **LOCKED — agreed between user and implementer. Changes require a new
discussion.**

## Vision

Make the pointer impossible to lose on niri. A high-contrast ring tracks the
cursor everywhere — REAPER, browser, overview — toggled instantly with
Shift+F12. Built so the enabling piece (cursor position over IPC) can be
upstreamed and the fork can die.

## Scope

**MVP**

- Patched niri: `cursor-position` IPC request + pointer-position events on the
  event-stream, plus a `niri msg cursor-position` subcommand.
- Overlay daemon: ring drawn on every output, following the cursor in real
  time, click-through, never stealing keyboard.
- Toggle on/off via Shift+F12 (niri bind → CLI), with `status`, `reload`,
  `stop` commands.
- Config file for ring size, thickness, color, glow, opacity, fade.
- Autostart via niri `spawn-at-startup`.

**Later**

- Upstream PR to niri; rebuild script against new niri tags.
- Presets (presentation mode: giant ring; audio mode: subtle ring).
- Adapter for other compositors that expose cursor position (Hyprland) behind
  the same daemon seam.
- Packaging (COPR, AUR).

**Explicitly out of scope**

- X11 session support (niri is Wayland-only).
- Compositors that hide cursor position with no escape hatch (GNOME under
  mutter, sway) — nothing to build on.
- A GUI settings app; config file is the interface.
- Pointer trails, click animations, key-visualizers — different tools.

## Architecture sketch

```
┌──────────────┐   event-stream (push, JSON lines)   ┌──────────────────┐
│ patched niri │ ──────────────────────────────────▶ │ ringworm daemon  │
│  (IPC socket)│ ◀────────────────────────────────── │ (layer-shell     │
└──────────────┘   cursor-position (on connect)      │  overlay, C)     │
        ▲                                            └────────┬─────────┘
        │ spawn "ringworm" toggle                     unix socket (toggle)
        │                                            ▼
   niri config.kdl                              ringworm CLI
   binds: Shift+F12                             (one-shot client)
```

State machine (daemon): `disabled → enabled → (fade-out) → disabled`.
Position feed: `live → stale(>200ms no event) → fade ring out` so a dead feed
never leaves a lying ring on screen.

## Stack

| Piece | Choice | Why |
|---|---|---|
| niri patch | Rust (niri's language) | must compile into niri; smallest diff |
| daemon | C11 + libwayland-client | zero heavy deps; distro-friendly; one file is honest |
| rendering | hand-rolled AA into wl_shm buffers, ×2 supersample | no cairo/egl; ring is 30 lines of math; crisp at fractional scales |
| IPC (compositor) | niri event-stream push | no polling CPU; falls back to request on connect |
| control | unix socket in `$XDG_RUNTIME_DIR` + one-shot CLI | works from niri binds, REAPER scripts, shell; single-instance via bind |
| build | Makefile (daemon), cargo (niri) | boring on purpose |

## Milestones

| # | Milestone | Acceptance |
|---|---|---|
| M0 | Scaffold: repo, docs skeleton, vendored protocol | repo commits cleanly; docs map resolves |
| M1 | niri patch: cursor position over IPC + event-stream | patched niri builds from clean upstream tag; `niri msg cursor-position` tracks the live pointer; event-stream emits position events |
| M2 | Daemon renders ring from the feed | `niri msg layers` shows ringworm surfaces on all outputs; ring visibly tracks cursor over every app incl. REAPER; idle CPU ≈ 0 |
| M3 | Styling + config file | changing radius/color/thickness/glow in `ringworm.conf` + `ringworm reload` visibly changes the ring; bad values rejected with message |
| M4 | Toggle + autostart + robustness | Shift+F12 toggles; survives output hotplug (HDMI script); feed-stall fades ring; single instance enforced |
| M5 | Open-source polish | MIT license, README with demo GIF, `scripts/build-niri.sh` reproducible against pinned tag, patch series PR-ready |

Order note: M1 is first after scaffold because it's the only true unknown and
everything else is testable against it. The daemon has a headless seam (it can
log positions from the IPC feed with `--no-render`) so M1 acceptance and daemon
development overlap safely.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| niri 26.04 internals differ from assumptions | M1 starts by reading upstream source at the pinned tag; patch kept minimal |
| Rust toolchain + build deps need sudo | install list prepared; single `dnf install` transaction |
| niri updates silently break the patch | `scripts/build-niri.sh` pins the tag and fails loudly; upstream PR ends the risk |
| Ring lags at fractional scale / multi-output | ×2 supersampled buffers; per-output surfaces; HDMI hotplug tested (owner has a toggle script) |
| Event feed stalls (patch regression) | staleness watchdog fades the ring instead of freezing it mid-screen |

## Locked decisions

- Option A chosen over cursor-theme and input-sniffing approaches (owner,
  discovery session): overlay must work over REAPER's custom cursors, so the
  ring must be compositor-level, and the position must come from niri itself.
- Upstreaming the IPC patch is the endgame; the fork is a means, not a home.
- Toggle keybind: Shift+F12, registered in the user's niri config (not by the
  daemon).
