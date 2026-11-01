#!/usr/bin/env bash
# Builds a patched niri binary from the pinned upstream tag + niri-patch/.
#
# Result: niri/target/release/niri — run it instead of the system niri (same
# config file, same IPC socket naming).
#
# Fedora build deps (one-time):
#   sudo dnf install -y rust cargo clang lld wayland-devel libinput-devel \
#       libxkbcommon-devel libseat-devel libdisplay-info-devel dbus-devel \
#       systemd-devel pipewire-devel pango-devel cairo-gobject-devel \
#       mesa-libEGL-devel mesa-libgbm-devel
set -euo pipefail

TAG="v26.04"
UPSTREAM="https://github.com/YaLTeR/niri.git"
BRANCH="ringworm-cursor-position"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/niri"

if [ ! -d "$DIR" ]; then
    git clone --depth 1 --branch "$TAG" "$UPSTREAM" "$DIR"
fi

cd "$DIR"

if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
    git checkout "$BRANCH"
else
    git checkout -b "$BRANCH"
    git am "$ROOT"/niri-patch/*.patch
fi

echo "building niri (this takes a few minutes on first run)..."
cargo build --release --locked

echo
echo "patched binary: $DIR/target/release/niri"
"$DIR/target/release/niri" --version
echo
echo "Point your session at it (see README 'Install'), then check with:"
echo "  niri msg cursor-position"
