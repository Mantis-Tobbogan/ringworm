# ADR 0004: Daemon is C11 + libwayland-client only; Rust lives only in the niri patch

Status: Accepted
Date: 2026-07-18

## Context

The daemon needs: a Wayland connection, layer-shell, shm buffers, a unix
socket, and a JSON-lines reader for niri's event-stream. Candidate stacks:
Rust (smithay-client-toolkit), Python (pywayland), C (raw libwayland).

## Decision

C11 against raw `libwayland-client`, hand-rolled rendering, a ~100-line
JSON-line scanner for the flat event objects niri emits. Vendored protocol
XML compiled with `wayland-scanner`. No cairo, no egl, no glib.

## Rationale

The ring is arithmetic (`sqrt`, `exp`, source-over blend) — a graphics stack
would outweigh the program. Raw libwayland means the binary is a few hundred
KB, depends on one library users already have, and builds anywhere in seconds.
Python was rejected because pywayland codegen for layer-shell adds setup pain
for a program that must eventually be packaged for strangers. Rust/sctk was
rejected because the patch already forces contributors to have a Rust
toolchain for niri; the daemon should not compound that requirement.

## Consequences

- Protocol XML drift is manual — vendored files must be reviewed when niri
  bumps protocol versions (tracked in AGENTS.md architecture notes).
- JSON parsing is schema-specific; if niri's event format changes, the scanner
  is updated in the same commit as the patch bump.
