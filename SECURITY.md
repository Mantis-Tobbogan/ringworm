# Security policy

## Supported versions

Ringworm is pre-1.0; only the latest `main` is supported. The daemon and the
niri patch series are versioned together (see `CHANGELOG.md`).

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Use GitHub's
private reporting: **Security → Report a vulnerability** on
[Mantis-Tobbogan/ringworm](https://github.com/Mantis-Tobbogan/ringworm/security/advisories/new).

Include the ringworm version (`ringworm --version`), the niri version
(`niri msg version`), and what you observed. You can expect an initial reply
within a week.

## What counts

The daemon is a Wayland client with two IPC surfaces, so the interesting
attack surface is small but real:

- **niri IPC input.** The daemon parses line-delimited JSON from niri's event
  stream. Malformed input should never crash the daemon or corrupt memory;
  parser bugs are security issues.
- **Control socket.** `$XDG_RUNTIME_DIR/ringworm.sock` accepts a fixed set of
  commands (`toggle`, `on`, `off`, `status`, `reload`, `stop`). It is created
  mode `0600` inside the user's private runtime directory. Anyone who can
  reach that socket can already run commands as you, but a way to escalate
  beyond that would be a vulnerability.
- **Config file.** `~/.config/ringworm/ringworm.conf` is parsed with clamping;
  a malicious config should not be able to crash the daemon or draw outside
  its buffers.

Not in scope: the fact that the daemon needs a patched compositor, or that
`niri-patch/` changes niri itself (report niri bugs to
[niri](https://github.com/YaLTeR/niri/security) once upstreamed).

## Hardening notes

- The daemon never reads `/dev/input`, never grabs the keyboard, and sets an
  empty input region on every surface — it cannot see your input.
- It writes to niri's IPC socket exactly once per connection (the request and
  the event-stream subscription), then only reads.
