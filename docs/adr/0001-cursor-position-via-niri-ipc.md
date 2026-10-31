# ADR 0001: Cursor position comes from a patched niri IPC + event-stream

Status: Accepted
Date: 2026-07-18

## Context

Every cursor-follow overlay needs a global cursor position. On this
environment (niri 26.04 + xwayland-satellite) every client-visible source was
investigated and rejected:

- niri exposes no pointer position over IPC or Wayland protocol (verified via
  `niri msg --help`, advertised protocol list, binary strings).
- XWayland coordinates under xwayland-satellite are a fictional canvas;
  satellite cannot know where niri places windows, so `XQueryPointer` values
  do not map to screen positions.
- `/dev/input` integration drifts: the machine has touchpad + TrackPoint +
  mouse with compositor-side acceleration, and clamping at screen edges makes
  integration diverge irrecoverably.
- niri's window JSON reports `tile_pos_in_workspace_view: null`, so anchoring
  to a window's known position is impossible.

Alternative compositors prove the seam is the right shape: Hyprland ships
`hyprctl cursorpos`, KWin scripts expose `workspace.cursorPos`.

## Decision

Patch niri to expose the pointer position two ways: a `cursor-position`
IPC request (with matching `niri msg` subcommand) and pointer-position events
on the existing event-stream. The daemon consumes the event-stream push.

## Rationale

The compositor is the only component that truthfully knows the pointer
position. Any client-side reconstruction is a drift-prone approximation. niri
is source-available and small; a minimal IPC addition is a day of work and a
candidate for upstream merge, after which Ringworm needs no fork at all.

## Consequences

- Running Ringworm requires a patched niri until upstream accepts the patch.
- Patch must stay minimal and PR-ready (see AGENTS.md invariant).
- niri version updates require a rebuild against a pinned tag until merged
  upstream.
