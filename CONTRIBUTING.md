# Contributing to Ringworm

Thanks for taking a look. Ringworm is small on purpose: a C daemon with one
dependency, and a patch series that has to stay acceptable to niri upstream.

## Ground rules

- **Zero-warning builds.** `make -C daemon check` must pass with
  `-Wall -Wextra -Werror`. Warnings are bugs here.
- **The daemon never polls the pointer** and **never writes to the IPC socket
  after subscribing** — see [ADR 0005](docs/adr/0005-liveness-is-the-connection.md).
  A resting cursor is not staleness.
- **Every niri patch must stay PR-ready.** Smallest possible diff, conventional
  commit subject, a body that explains *why*. If a patch would not survive
  review on `YaLTeR/niri`, it does not belong here.
- **Vendored protocol XML is never hand-edited.** Request opcodes are
  positional; copy canonical files instead.

## Building and testing

```sh
make -C daemon check          # daemon, from scratch, warnings are errors
make -C scripts               # virtmouse + the demo renderer

./daemon/ringworm --fake      # ring follows a sine wave, no niri patch needed
./daemon/ringworm -v          # real feed; needs a patched niri (NIRI_SOCKET)
```

`scripts/virtmouse x y` injects pointer motion through
`zwlr_virtual_pointer`, which makes the feed testable without a human moving a
mouse:

```sh
make -C scripts virtmouse
./scripts/virtmouse 700 600
niri msg cursor-position      # should report the new position
```

For anything touching rendering, check fractional scale and more than one
output before opening a PR. `ringworm status` reports the feed and output
count; `niri msg layers | grep ringworm` should list one surface per output.

## Docs and media

- Behaviour changes go into the same commit as the code: update
  [`docs/plan.md`](docs/plan.md) notes, add or amend an ADR if a decision
  changed, and add a `CHANGELOG.md` entry.
- The README images are generated from the real ring math:

  ```sh
  make -C scripts demo        # ImageMagick 7; writes docs/media/
  ```

  Do not commit screen recordings of your own desktop.

## Commits

[Conventional Commits](https://www.conventionalcommits.org/), lowercase
imperative subject, why-body wrapped at ~72 columns:

```
type(scope): summary

Why this change is needed and what it trades off.
```

Types: `feat`, `fix`, `docs`, `chore`, `build`, `refactor`, `test`. Scopes
mirror top-level directories (`daemon`, `niri-patch`, `scripts`, `docs`).

## Pull requests

- One logical change per PR; keep refactors away from behaviour changes.
- Say how you tested it (niri version, outputs, scale, `ringworm status`).
- CI must be green: the daemon builds warning-free and the patch series
  applies to the pinned niri tag.

By contributing you agree that your work is licensed under the MIT license
for everything outside `niri-patch/`, and GPL-3.0-or-later for patches (niri's
license).
