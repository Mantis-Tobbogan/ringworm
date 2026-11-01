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
Position feed: liveness is the IPC connection itself (ADR 0005) — the socket
closing drops the ring, a resting cursor is not staleness.

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
| M0 ✅ | Scaffold: repo, docs skeleton, vendored protocol | repo commits cleanly; docs map resolves |
| M1 ✅ | niri patch: cursor position over IPC + event-stream | patched niri builds from clean upstream tag; `niri msg cursor-position` tracks the live pointer; event-stream emits position events |
| M2 ✅ | Daemon renders ring from the feed | `niri msg layers` shows ringworm surfaces on all outputs; ring visibly tracks cursor over every app incl. REAPER; idle CPU ≈ 0 |
| M3 ✅ | Styling + config file | changing radius/color/thickness/glow in `ringworm.conf` + `ringworm reload` visibly changes the ring; bad values rejected with a message |
| M4 ✅ | Toggle + autostart + robustness | Shift+F12 toggles; survives output hotplug (HDMI script); feed death drops the ring (ADR 0005); single instance enforced |
| M5 ✅ | Open-source polish | MIT license, README with demo GIF, `scripts/build-niri.sh` reproducible against pinned tag, patch series PR-ready |

### notes (implemented, M5 polish)

- License split: the daemon/scripts/docs are MIT, but `niri-patch/` is a
  derivative of niri (GPL-3.0) and ships as GPL-3.0-or-later with niri's
  license text copied in. Vendored protocol XMLs keep their inline permissive
  licenses. A root `LICENSE` documents the scope of each.
- Daemon and virtmouse now build `-Werror` clean; unix-socket paths are
  bounds-checked (the old snprintf pattern warned on every build).
- Config problems are reported instead of silently clamped: unknown keys,
  non-numeric values and out-of-range values are listed by `ringworm reload`
  (and logged at startup), so a typo in `ringworm.conf` is visible.
- The ring math moved to `daemon/ring.c` (pure functions, no Wayland) so the
  README media can be rendered offline from exactly what ships.
- Demo GIF and hero still are generated from the real ring math by
  `make -C scripts demo` (`scripts/render-demo.c` + ImageMagick), so the
  README media cannot drift from the renderer and no private desktop ends up
  in the repo. Everything else in the M5 acceptance line is done.

### notes (implemented, M1+M2 core)

- M1 ✅ patch series builds clean against v26.04; `niri msg cursor-position`
  returns live coords, `--json` prints `[x,y]`; event-stream pushes
  `CursorPositionChanged`; verified nested via injected virtual pointer
  (virtmouse (700,600) → compositor 651.0,646.2 → daemon identical).
- M2 ✅ core verified the same way: daemon overlays every output, ring tracks
  the feed live; CLI control plane works (`status`, `stop`).
- Wire-protocol lesson: never hand-trim a Wayland XML — request opcodes are
  positional and get_popup sits mid-sequence. Canonical XML is vendored from
  the wayland-protocols crates; xdg-shell is linked for symbol closure only.
- ~~Feed liveness was heartbeat-based (ping every 1s, stale at 3s)~~ —
  superseded by ADR 0005: a resting cursor produces no events, and writes
  after the event-stream subscription wedge niri's receive queue. Liveness is
  the connection; no heartbeat, no staleness window. Position events are
  push-only; the pointer is never polled.
- Nested-session note: pointer-enter without motion gives a position via the
  request but no event until the first real motion; invisible in practice.
- The `niri msg event-stream` printer needed an arm for the new event (exhaustive
  match); folded into the cli patch.

Order note: M1 is first after scaffold because it's the only true unknown and
everything else is testable against it. The daemon has a headless seam — `-v`
logs positions straight from the IPC feed, and `--fake` renders a synthetic
pointer with no niri patch at all — so M1 acceptance and daemon development
overlap safely.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| niri 26.04 internals differ from assumptions | M1 starts by reading upstream source at the pinned tag; patch kept minimal |
| Rust toolchain + build deps need sudo | install list prepared; single `dnf install` transaction |
| niri updates silently break the patch | `scripts/build-niri.sh` pins the tag and fails loudly; upstream PR ends the risk |
| Ring lags at fractional scale / multi-output | ×2 supersampled buffers; per-output surfaces; HDMI hotplug tested (owner has a toggle script) |
| Event feed stalls (patch regression) | the compositor closing the socket drops the ring; a hung compositor is accepted (ADR 0005) |

## Locked decisions

- Option A chosen over cursor-theme and input-sniffing approaches (owner,
  discovery session): overlay must work over REAPER's custom cursors, so the
  ring must be compositor-level, and the position must come from niri itself.
- Upstreaming the IPC patch is the endgame; the fork is a means, not a home.
- Toggle keybind: Shift+F12, registered in the user's niri config (not by the
  daemon).
