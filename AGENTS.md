# AGENTS.md

Operational card for coding agents working in this repo. Keep it thin; details
live in `docs/`.

## Commands

- Build daemon: `make -C daemon` → `daemon/ringworm`
- Run daemon (foreground, verbose): `daemon/ringworm -v`
- Toggle/status/stop: `ringworm toggle | on | off | status | reload | stop`
- Verify overlay exists: `niri msg layers | grep ringworm`
- Build patched niri (M1+): `scripts/build-niri.sh` (clones upstream, applies
  `niri-patch/*.patch`, builds — needs rust toolchain)
- Verify patch: `niri msg cursor-position` (coords must track the live pointer)

## Architecture

- `daemon/` — the ring. `src/main.c` single binary: Wayland layer-shell
  overlay (click-through, top layer, one surface per output) + niri IPC client
  (event-stream push, line-based JSON) + control socket for toggle.
  Invariant: **the daemon never polls the pointer; positions arrive as
  event-stream push.** Invariant: empty input region — clicks always pass
  through.
- `niri-patch/` — patch series against a pinned upstream niri tag. Smallest
  possible diff; every patch must stay PR-ready for upstream niri.
- `protocol/` — vendored Wayland protocol XML the daemon is built against.
- `docs/` — plan (the contract), ADRs (immutable decisions), doc map in
  `docs/README.md`.

## Conventions

- C11, gcc, no deps beyond `libwayland-client` (+ `libxkbcommon` never needed
  here). No cairo/egl — rings are drawn as AA math into shm buffers.
- Conventional Commits, lowercase imperative, why-body wrapped ~72 cols.
- One commit per milestone; docs written backwards onto the work (plan ticks +
  "notes (implemented)" after each milestone).
- Config file: `~/.config/ringworm/ringworm.conf`, key=value, safe defaults.

## Commits

`type(scope): summary` — types: feat, fix, docs, chore, build. Scope mirrors
top-level dirs (`daemon`, `niri-patch`, `docs`). The subject is the what, the
body is the why.
