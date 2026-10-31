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

1. **`niri-patch/`** — a small patch for niri that exposes the cursor position
   over niri's IPC and event-stream. This is the part we upstream; once merged,
   nobody needs a fork.
2. **`daemon/`** — a click-through layer-shell overlay that draws the ring on
   the top layer of every output, fed live by the patched IPC. Pure C,
   libwayland-client only.

Status: **work in progress — see [docs/plan.md](docs/plan.md).**
