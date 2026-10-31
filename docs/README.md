# Ringworm docs

Every file answers exactly one kind of question.

| File | Question it answers | Reader |
|---|---|---|
| [plan.md](plan.md) | What are we building, in what order, and what's locked? | implementer + owner |
| [adr/](adr/) | Why is it built *this* way and not the obvious way? | future maintainers, upstream reviewers |
| [../README.md](../README.md) | What is this thing? | everyone else |
| [../AGENTS.md](../AGENTS.md) | How do agents work in this repo day-to-day? | coding agents |

## ADR index

| ADR | Decision | Status |
|---|---|---|
| [0001](adr/0001-cursor-position-via-niri-ipc.md) | Cursor position comes from a patched niri IPC + event-stream | Accepted |
| [0002](adr/0002-layer-shell-overlay-renderer.md) | Ring renders as a click-through wlr-layer-shell top-layer overlay | Accepted |
| [0003](adr/0003-toggle-via-unix-socket-and-niri-bind.md) | Toggle = niri keybind → one-shot CLI over a daemon unix socket | Accepted |
| [0004](adr/0004-daemon-in-c-no-cairo-egl.md) | Daemon is C11 + libwayland-client only; Rust lives only in the niri patch | Accepted |
