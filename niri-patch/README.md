# niri-patch

Patch series against [niri](https://github.com/YaLTeR/niri) tag `v26.04`
that exposes the cursor position over niri's IPC: a `CursorPosition`
request (plus `niri msg cursor-position`), and `CursorPositionChanged`
events on the event stream, included in the up-front snapshot.

These patches are a derivative work of niri and are licensed
**GPL-3.0-or-later** (see `LICENSE`, copied from niri). The rest of
Ringworm is MIT — the split exists precisely so the daemon can stay
permissive while the patches follow niri's license.

Apply with `scripts/build-niri.sh`, or by hand:

```sh
git clone --depth 1 --branch v26.04 https://github.com/YaLTeR/niri
cd niri && git am ../niri-patch/*.patch && cargo build --release
```

The endgame is upstream: if/when the cursor-position IPC lands in niri,
this directory retires.
