#!/usr/bin/env bash
# Regenerates docs/media/demo.gif and docs/media/ring.png.
#
# The frames come from scripts/render-demo.c, which links the daemon's real
# ring math (daemon/ring.c) — so the demo cannot drift from what ships, and
# nobody has to screen-record their desktop to update the README.
#
# Requires: ImageMagick 7 (`magick`), cc, make. No compositor, no niri.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/media"
# frames and GIF are rendered at WxH; the backdrop is designed in DWxDH and
# scaled once, so the layout numbers below stay readable
W=800
H=450
DW=960
DH=540
FRAMES=60
DELAY=4          # centiseconds per frame -> 25 fps

# ring style used in the demo (a touch bolder than the defaults so it reads
# at README size)
RADIUS=26 THICKNESS=5 GLOW=9 COLOR=ffb000 ALPHA=90

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

make -s -C "$ROOT/scripts" render-demo
mkdir -p "$OUT" "$TMP/frames" "$TMP/png"

"$ROOT/scripts/render-demo" "$TMP/frames" "$W" "$H" "$FRAMES" \
	--radius "$RADIUS" --thickness "$THICKNESS" --glow "$GLOW" \
	--color "$COLOR" --alpha "$ALPHA"

# --- synthetic desktop backdrop -------------------------------------------
# Abstract on purpose: it shows the ring over a terminal, a browser and a
# DAW-like timeline without putting anyone's real screen in the repo.
# ImageMagick cannot render variable fonts, so resolve a static face by path.
font_file() {
	local f
	f="$(fc-match -f '%{file}' "$1" 2>/dev/null | head -1)"
	if [ -n "$f" ] && [ -f "$f" ]; then printf '%s' "$f"; else printf '%s' "$2"; fi
}
FONT="$(font_file 'Liberation Sans' Helvetica)"
MONO="$(font_file 'Liberation Mono' Courier)"

draw=()
add() { draw+=("$@"); }

# panel
add -fill '#0a0e13' -draw "rectangle 0,0 $DW,30"
add -fill '#2a3340' -draw "rectangle 0,29 $DW,30"
add -fill '#ffb000' -draw "roundrectangle 12,9 30,21 3,3"
add -fill '#8b98a9' -font "$MONO" -pointsize 11 -gravity NorthWest \
	-annotate +38+9 'ringworm  Shift+F12'
add -fill '#5c6879' -font "$MONO" -pointsize 11 -gravity NorthEast \
	-annotate +14+9 '12:04'
add -gravity NorthWest

# terminal window
add -fill '#0f141b' -stroke '#2b3441' -strokewidth 1 \
	-draw "roundrectangle 48,56 448,352 8,8"
add -fill '#161d26' -stroke none -draw "roundrectangle 48,56 448,84 8,8"
add -fill '#161d26' -draw "rectangle 48,74 448,84"
add -fill '#3a4553' -draw "circle 66,70 66,73"
add -fill '#3a4553' -draw "circle 80,70 80,73"
add -fill '#3a4553' -draw "circle 94,70 94,73"
add -fill '#7d8a9c' -font "$MONO" -pointsize 10 -gravity NorthWest \
	-annotate +110+64 '~/src/ringworm — make'

lines=(
	'$ make -C daemon'
	'wayland-scanner client-header ../protocol/…'
	'cc -O2 -std=c11 -Wall -Wextra -Werror …'
	'$ ./ringworm -v'
	'ringworm: config loaded from ~/.config/ringworm/…'
	'ringworm: control socket: /run/user/1000/ringworm.sock'
	'ringworm: ipc connected: /run/user/1000/niri.…sock'
	'ringworm: ipc position 812.0,404.0; subscribing…'
	'ringworm: 2 outputs overlaid'
)
y=98
i=0
for l in "${lines[@]}"; do
	color='#c9d4e2'
	if [ "$i" -eq 0 ] || [ "$i" -eq 3 ]; then color='#8fe0a6'; fi
	if [ "$i" -eq 1 ] || [ "$i" -eq 2 ]; then color='#6b7789'; fi
	add -fill "$color" -font "$MONO" -pointsize 11 -gravity NorthWest \
		-annotate +64+$y "$l"
	y=$((y + 20))
	i=$((i + 1))
done

# browser window
add -fill '#121820' -stroke '#2b3441' -strokewidth 1 \
	-draw "roundrectangle 500,84 912,384 8,8"
add -fill '#161d26' -stroke none -draw "roundrectangle 500,84 912,112 8,8"
add -fill '#161d26' -draw "rectangle 500,102 912,112"
add -fill '#0d1218' -draw "roundrectangle 512,116 900,136 6,6"
add -fill '#8b98a9' -font "$FONT" -pointsize 10 -gravity NorthWest \
	-annotate +524+120 'github.com/YaLTeR/niri  —  IPC.md'
add -fill '#e6edf6' -font "$FONT" -pointsize 13 -gravity NorthWest \
	-annotate +520+150 'niri IPC'
for k in 0 1 2 3 4 5; do
	wdt=$((360 - k * 17))
	[ "$k" -eq 4 ] && wdt=180
	add -fill '#39424f' -draw "rectangle 520,$((178 + k * 16)) $((520 + wdt)),$((184 + k * 16))"
done
add -fill '#1c2733' -stroke '#2b3441' -draw "roundrectangle 520,278 892,368 6,6"
add -fill '#ffb000' -font "$MONO" -pointsize 10 -gravity NorthWest \
	-annotate +532+288 'cursor-position'
add -fill '#7d8a9c' -font "$MONO" -pointsize 10 -gravity NorthWest \
	-annotate +532+306 '2371.0, 428.0'

# DAW-like timeline
add -fill '#10151c' -stroke '#2b3441' -strokewidth 1 \
	-draw "roundrectangle 48,396 912,512 8,8"
add -fill '#161d26' -stroke none -draw "rectangle 48,396 912,416"
add -fill '#8b98a9' -font "$FONT" -pointsize 10 -gravity NorthWest \
	-annotate +60+400 'REAPER — track 1'
for k in $(seq 0 55); do
	x=$((60 + k * 15))
	hgt=$((6 + (k * 37 % 46)))
	add -fill '#2f6f5e' -draw "rectangle $x,$((470 - hgt)) $((x + 8)),470"
done
add -fill '#1b2430' -draw "rectangle 60,474 900,486"
add -fill '#ffb000' -draw "rectangle 60,474 $((60 + 380)),486"

base="$TMP/base.png"
magick -size ${DW}x${DH} gradient:'#0d1117-#171f2a' \
	"${draw[@]}" -resize ${W}x${H}! "$base"

# --- composite frames over the backdrop -----------------------------------
for f in "$TMP"/frames/*.rgba; do
	name="$(basename "$f" .rgba)"
	magick "$base" \( -size ${W}x${H} -depth 8 "rgba:$f" \) \
		-composite "$TMP/png/$name.png"
done

# --- hero still -----------------------------------------------------------
# A dedicated 1:1 render on a plain backdrop: shows the anti-aliasing up close
# without cropping window text. Four frames so the pointer lands dead centre.
mkdir -p "$TMP/hero"
"$ROOT/scripts/render-demo" "$TMP/hero" 420 420 1 --still \
	--radius "$RADIUS" --thickness "$THICKNESS" --glow "$GLOW" \
	--color "$COLOR" --alpha "$ALPHA" >/dev/null
magick -size 420x420 gradient:'#131a23-#1e2836' \
	\( -size 420x420 -depth 8 "rgba:$TMP/hero/f0000.rgba" \) \
	-composite "$OUT/ring.png"

# --- GIF ------------------------------------------------------------------
# `-layers Optimize` (not OptimizePlus) is what crops per-frame to the changed
# region; it keeps the file around 1 MB instead of 3 MB.
magick -delay "$DELAY" -loop 0 "$TMP"/png/*.png \
	-layers Optimize "$OUT/demo.gif"

printf 'demo: %s (%s)\n' "$OUT/demo.gif" "$(du -h "$OUT/demo.gif" | cut -f1)"
printf 'hero: %s (%s)\n' "$OUT/ring.png" "$(du -h "$OUT/ring.png" | cut -f1)"
