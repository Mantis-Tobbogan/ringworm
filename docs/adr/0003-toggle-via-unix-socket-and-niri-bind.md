# ADR 0003: Toggle = niri keybind → one-shot CLI over a daemon unix socket

Status: Accepted
Date: 2026-07-18

## Context

The owner wants Shift+F12 to toggle the ring. On niri, applications cannot
grab global keys; niri's own `binds` are the only global hotkey mechanism.

## Decision

The user's `config.kdl` binds Shift+F12 to `spawn "ringworm" "toggle"`. The
CLI connects to `$XDG_RUNTIME_DIR/ringworm.sock`, sends the command line, prints
the daemon's reply, exits. Commands: `toggle`, `on`, `off`, `status`,
`reload`, `stop`. The socket bind doubles as the single-instance lock
(`EADDRINUSE` → probe with connect: live daemon → exit quietly; dead socket →
unlink and take over). `SIGHUP` reloads config; `SIGUSR1` toggles.

## Rationale

Keybinds belong to the compositor's config (the user already lives in
`config.kdl`); a socket CLI makes the same toggle reachable from REAPER
scripts, shell, and SSH — no D-Bus, no portal dependency.

## Consequences

- Shift+F12 is consumed system-wide while bound (owner accepted; unused in
  REAPER's default map).
- Daemon must keep the socket responsive from the main loop (no blocking).
