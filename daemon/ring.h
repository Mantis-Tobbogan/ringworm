/* ringworm — ring geometry and pixel math.
 *
 * SPDX-License-Identifier: MIT
 *
 * Pure functions, no Wayland, no globals: the same code draws the live
 * overlay and the README demo frames (scripts/render-demo.c).
 *
 * All coordinates here are *buffer* pixels, i.e. twice the logical pixels:
 * surfaces are supersampled x2 (see docs/adr/0002).
 */
#ifndef RINGWORM_RING_H
#define RINGWORM_RING_H

#include <stddef.h>
#include <stdint.h>

struct ring_style {
	int radius;      /* logical px to the ring centerline */
	int thickness;   /* logical px */
	int glow;        /* logical px of soft falloff outside the ring */
	uint32_t color;  /* 0xRRGGBB */
};

/* Half-size of the square a ring can touch, in buffer px, measured from the
 * ring center. Used to clip and to compute damage rectangles. */
int ring_extent(const struct ring_style *s);

/* Draw the ring centred at (cx, cy) into an ARGB8888 buffer, clipped to
 * [x1, x2) x [y1, y2). `alpha` is 0..1 and already includes the configured
 * opacity. Pixels outside the ring's extent are left untouched. */
void ring_draw(uint32_t *pixels, int stride, const struct ring_style *s,
	double cx, double cy, double alpha, int x1, int y1, int x2, int y2);

#endif
