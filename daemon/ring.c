/* ringworm — ring geometry and pixel math.
 *
 * SPDX-License-Identifier: MIT
 *
 * A ring is three source-over layers evaluated per pixel: a soft glow, a dark
 * contrast band just outside the stroke (so the ring reads on light and dark
 * backgrounds alike), and an anti-aliased core stroke.
 */
#include "ring.h"

#include <math.h>

int ring_extent(const struct ring_style *s) {
	return s->radius + s->thickness + s->glow + 3;
}

void ring_draw(uint32_t *pixels, int stride, const struct ring_style *s,
	double cx, double cy, double alpha, int x1, int y1, int x2, int y2) {
	if (alpha <= 0.004) return;

	/* buffer px: surfaces are supersampled x2 */
	double rr = s->radius * 2.0, tt = s->thickness * 2.0, gg = s->glow * 2.0;
	if (gg < 1.0) gg = 1.0;
	double cr = ((s->color >> 16) & 255) / 255.0;
	double cg = ((s->color >> 8) & 255) / 255.0;
	double cb = (s->color & 255) / 255.0;
	double inv2g2 = 1.0 / (2.0 * gg * gg);

	for (int y = y1; y < y2; y++) {
		uint32_t *row = pixels + (size_t)y * (size_t)stride;
		double dy = y + 0.5 - cy;
		for (int x = x1; x < x2; x++) {
			double dx = x + 0.5 - cx;
			double e = sqrt(dx * dx + dy * dy) - rr;
			double ae = fabs(e);

			double t = tt * 0.5 + 1.0 - ae;
			t = t < 0 ? 0 : (t > 1 ? 1 : t);
			double core = t * t * (3 - 2 * t);          /* smoothstep AA */

			double d2 = ae - (tt * 0.5 + 2.0);
			if (d2 < 0) d2 = -d2;
			double dark = 1.8 - d2;
			dark = (dark < 0 ? 0 : (dark > 1 ? 1 : dark)) * 0.55;

			double glow = exp(-(e * e) * inv2g2) * (e > 0 ? 0.32 : 0.10);

			/* source-over stack: glow, dark contrast, core */
			double ag = glow * alpha;
			double ad = dark * alpha * 0.9;
			double ac = core * alpha;
			double r = 0, g = 0, b = 0, a = 0;
			r = cr * ag + r * (1 - ag); g = cg * ag + g * (1 - ag);
			b = cb * ag + b * (1 - ag); a = ag + a * (1 - ag);
			r = r * (1 - ad); g = g * (1 - ad); b = b * (1 - ad);
			a = ad + a * (1 - ad);
			r = cr * ac + r * (1 - ac); g = cg * ac + g * (1 - ac);
			b = cb * ac + b * (1 - ac); a = ac + a * (1 - ac);

			uint32_t r8 = (uint32_t)(r * a * 255.0 + 0.5);
			uint32_t g8 = (uint32_t)(g * a * 255.0 + 0.5);
			uint32_t b8 = (uint32_t)(b * a * 255.0 + 0.5);
			uint32_t a8 = (uint32_t)(a * 255.0 + 0.5);
			row[x] = (a8 > 255 ? 255u : a8) << 24 | (r8 & 255) << 16 |
				(g8 & 255) << 8 | (b8 & 255);
		}
	}
}
