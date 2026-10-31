# ADR 0002: Ring renders as a click-through wlr-layer-shell top-layer overlay

Status: Accepted
Date: 2026-07-18

## Context

The ring must be visible above all windows, including XWayland apps (REAPER)
and full-screen surfaces, while never intercepting input and never grabbing
keyboard.

## Decision

One `zwlr_layer_surface_v1` per output: layer `top`, anchored to all edges
(`set_exclusive_zone(-1)`), `keyboard_interactivity: none`, and an **empty
input region** on the wl_surface. Content is drawn into ×2-supersampled
`wl_shm` ARGB buffers with damage only around the ring's bounding boxes.

## Rationale

Layer-shell is the only Wayland-blessed way to get compositor-arranged
overlay surfaces, and niri implements it. Empty input region is the protocol's
supported click-through mechanism — no X11 shape hacks, no input stealing.
Supersampling gives clean anti-aliasing at fractional scales (owner runs
scale 1.5) without cairo/egl.

## Consequences

- The ring can be occluded only by other top-layer surfaces committing later
  (panels) — acceptable; when the pointer is over a Wayland-native panel the
  ring is at the pointer anyway.
- Output hotplug must be handled live (destroy/recreate surfaces on
  `wl_output` global add/remove) — the owner swaps monitors daily.
