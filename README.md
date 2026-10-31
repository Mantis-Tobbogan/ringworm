# Ringworm

A high-visibility ring around the mouse cursor for [niri](https://github.com/YaLTeR/niri) —
built for audio work (REAPER), presentations, and low-vision accessibility.

The cursor is impossible to lose. Toggle it with `Shift+F12`.

```
   ╭───╮
  (  ↑  )     ← your cursor, wearing a ring
   ╰───╯
```

## How it works

Wayland compositors hide the global cursor position from clients — for good
privacy reasons, but it also means no overlay tool can follow the cursor.
niri (unlike Hyprland or KWin) has no escape hatch either. Ringworm has two
parts:

1. **`niri-patch/`** — a small patch series for niri that exposes the cursor
   position over niri's IPC: a `CursorPosition` request, a
   `CursorPositionChanged` event on the event stream, and a
   `niri msg cursor-position` subcommand. This is the part we upstream; once
   merged, nobody needs a fork.
2. **`daemon/`** — `ringworm`, a click-through layer-shell overlay (pure C,
   libwayland-client only) that draws the ring on the top layer of every
   output, fed live by the patched IPC. It never takes input, never grabs
   keyboard, and idles at ~0% CPU.

## Requirements

- Linux, [niri](https://github.com/YaLTeR/niri) ≥ 26.04 (wlr-layer-shell)
- gcc or clang, `make`, `pkg-config`, `wayland-scanner`, `libwayland-client`
- Rust toolchain + niri build deps, only to build the patched compositor
  (`scripts/build-niri.sh` lists them)

## Install

```sh
# 1. the daemon
make -C daemon
make -C daemon install          # → ~/.local/bin/ringworm

# 2. the patched compositor (a few minutes on first build)
./scripts/build-niri.sh         # → niri/target/release/niri
```

Then point your session at the patched binary and autostart the daemon:

```sh
# systemd user override (adjust the path to where you installed niri)
mkdir -p ~/.config/systemd/user/niri.service.d
printf '[Service]\nExecStart=\nExecStart=%s/.local/bin/niri --session\n' "$HOME" \
  > ~/.config/systemd/user/niri.service.d/override.conf
systemctl --user daemon-reload
```

```kdl
# ~/.config/niri/config.kdl
spawn-at-startup "ringworm"

binds {
    Shift+F12 hotkey-overlay-title="Toggle cursor ring" { spawn "ringworm" "toggle"; }
}
```

Log out and back in. The daemon waits in the background — the ring appears
only when you summon it with `Shift+F12` (or `ringworm on`; set
`enabled = yes` in the config if you prefer it always-on).

## Configuration

`~/.config/ringworm/ringworm.conf` — edit and run `ringworm reload`;
changes apply live.

| key | default | meaning |
|---|---|---|
| `radius` | 16 | ring size, px to the ring centerline |
| `thickness` | 3 | ring stroke width, px |
| `glow` | 6 | soft halo width outside the ring (0 disables) |
| `color` | `ffb000` | hex RRGGBB |
| `alpha` | 70 | overall opacity percent |
| `fade_ms` | 150 | fade in/out duration |
| `enabled` | `no` | ring visible at startup; `yes` turns it on automatically |

## CLI

```
ringworm toggle | on | off | status | reload | stop
```

Anything that can spawn a process can drive the ring — niri keybinds,
REAPER scripts, SSH sessions.

## Troubleshooting

- **No ring?** `ringworm status` — `feed=disconnected` means the daemon
  can't reach niri's IPC (is `NIRI_SOCKET` set, or is niri patched?).
  `niri msg layers` should list `ringworm` surfaces.
- **Ring fades when the cursor rests?** Fixed in 0.1.0 — upgrade; a resting
  cursor is normal, not a dead feed.
- **Ring under panels?** The ring renders on the top layer; compositors may
  place later-committed top-layer surfaces above it.

## License

The daemon, scripts and docs are [MIT](LICENSE). The niri patches are a
derivative of niri (GPL-3.0) and are distributed under
[GPL-3.0-or-later](niri-patch/LICENSE). Vendored protocol XML files in
`protocol/` carry their original permissive licenses inline.

## Status

Working on the author's machine (niri 26.04, Fedora, fractional scale,
multi-output). See [docs/plan.md](docs/plan.md) for the roadmap and
[docs/adr/](docs/adr/) for the decisions that shaped it.
