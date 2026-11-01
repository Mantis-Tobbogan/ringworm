/* ringworm — offline demo renderer.
 *
 * SPDX-License-Identifier: MIT
 *
 * Renders the exact ring used by the daemon (daemon/ring.c) into raw RGBA
 * frames, with a pointer arrow on top, so the README demo can be regenerated
 * deterministically on any machine — no screen capture, no compositor, no
 * private desktop in the shot.
 *
 *   render-demo OUTDIR WIDTH HEIGHT FRAMES [--radius N] [--thickness N]
 *               [--glow N] [--color RRGGBB] [--alpha PERCENT]
 *
 * Frames are written as OUTDIR/f0000.rgba (R,G,B,A bytes, straight alpha,
 * transparent background). scripts/make-demo-gif.sh composites them over a
 * synthetic desktop and assembles the GIF.
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../daemon/ring.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct canvas {
	uint32_t *px;   /* ARGB8888, same layout as the daemon's shm buffers */
	int w, h;
};

/* source-over a straight-alpha colour onto the canvas */
static void blend(struct canvas *c, int x, int y,
	double r, double g, double b, double a) {
	if (x < 0 || y < 0 || x >= c->w || y >= c->h || a <= 0.0) return;
	uint32_t *p = &c->px[(size_t)y * (size_t)c->w + (size_t)x];
	double da = (*p >> 24) / 255.0;
	double dr = ((*p >> 16) & 255) / 255.0;
	double dg = ((*p >> 8) & 255) / 255.0;
	double db = (*p & 255) / 255.0;
	double oa = a + da * (1 - a);
	if (oa <= 0.0) { *p = 0; return; }
	double orr = (r * a + dr * da * (1 - a)) / oa;
	double og = (g * a + dg * da * (1 - a)) / oa;
	double ob = (b * a + db * da * (1 - a)) / oa;
	uint32_t a8 = (uint32_t)(oa * 255.0 + 0.5);
	uint32_t r8 = (uint32_t)(orr * 255.0 + 0.5);
	uint32_t g8 = (uint32_t)(og * 255.0 + 0.5);
	uint32_t b8 = (uint32_t)(ob * 255.0 + 0.5);
	*p = (a8 > 255 ? 255u : a8) << 24 | (r8 & 255) << 16 |
		(g8 & 255) << 8 | (b8 & 255);
}

/* even-odd point-in-polygon */
static int inside(const double *pts, int n, double x, double y) {
	int in = 0;
	for (int i = 0, j = n - 1; i < n; j = i++) {
		double xi = pts[i * 2], yi = pts[i * 2 + 1];
		double xj = pts[j * 2], yj = pts[j * 2 + 1];
		if ((yi > y) != (yj > y) &&
			x < (xj - xi) * (y - yi) / (yj - yi) + xi)
			in = !in;
	}
	return in;
}

/* Fill a convex-or-not polygon with 2x2 supersampled coverage. Coordinates
 * are logical arrow units; (ox, oy) is the hotspot, `scale` sizes the arrow. */
static void fill_poly(struct canvas *c, const double *pts, int n,
	double ox, double oy, double scale, uint32_t rgb, double alpha) {
	double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
	double *sp = malloc(sizeof(double) * (size_t)n * 2);
	if (!sp) return;
	for (int i = 0; i < n; i++) {
		sp[i * 2] = ox + pts[i * 2] * scale;
		sp[i * 2 + 1] = oy + pts[i * 2 + 1] * scale;
		if (sp[i * 2] < minx) minx = sp[i * 2];
		if (sp[i * 2] > maxx) maxx = sp[i * 2];
		if (sp[i * 2 + 1] < miny) miny = sp[i * 2 + 1];
		if (sp[i * 2 + 1] > maxy) maxy = sp[i * 2 + 1];
	}
	double r = ((rgb >> 16) & 255) / 255.0;
	double g = ((rgb >> 8) & 255) / 255.0;
	double b = (rgb & 255) / 255.0;
	int x1 = (int)floor(minx), x2 = (int)ceil(maxx);
	int y1 = (int)floor(miny), y2 = (int)ceil(maxy);
	static const double so[4][2] = {
		{ 0.25, 0.25 }, { 0.75, 0.25 }, { 0.25, 0.75 }, { 0.75, 0.75 },
	};
	for (int y = y1; y < y2; y++) {
		for (int x = x1; x < x2; x++) {
			int hits = 0;
			for (int s = 0; s < 4; s++)
				if (inside(sp, n, x + so[s][0], y + so[s][1])) hits++;
			if (hits) blend(c, x, y, r, g, b, alpha * hits / 4.0);
		}
	}
	free(sp);
}

/* A conventional arrow pointer, hotspot at (0,0), ~13x21 logical units. */
static const double arrow[] = {
	0.0, 0.0,  0.0, 18.0,  4.4, 13.9,  7.4, 20.9,
	10.4, 19.4,  7.4, 12.4,  12.9, 12.4,
};

static void draw_pointer(struct canvas *c, double cx, double cy) {
	const double s = 1.45;
	fill_poly(c, arrow, (int)(sizeof arrow / sizeof *arrow / 2),
		cx, cy, s * 1.35, 0x05070a, 0.85);   /* outline / drop shadow */
	fill_poly(c, arrow, (int)(sizeof arrow / sizeof *arrow / 2),
		cx, cy, s, 0xf7f9fc, 1.0);
}

static int write_rgba(const char *path, const struct canvas *c) {
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	for (size_t i = 0; i < (size_t)c->w * (size_t)c->h; i++) {
		uint32_t p = c->px[i];
		unsigned char b[4] = {
			(unsigned char)((p >> 16) & 255),
			(unsigned char)((p >> 8) & 255),
			(unsigned char)(p & 255),
			(unsigned char)((p >> 24) & 255),
		};
		if (fwrite(b, 1, 4, f) != 4) { fclose(f); return -1; }
	}
	return fclose(f);
}

static void usage(void) {
	fprintf(stderr,
		"usage: render-demo OUTDIR WIDTH HEIGHT FRAMES [options]\n"
		"  --radius N --thickness N --glow N --color RRGGBB --alpha PERCENT\n");
}

int main(int argc, char **argv) {
	if (argc < 5) { usage(); return 2; }

	const char *outdir = argv[1];
	int w = atoi(argv[2]), h = atoi(argv[3]), frames = atoi(argv[4]);
	struct ring_style style = { .radius = 26, .thickness = 5, .glow = 9,
		.color = 0xffb000 };
	int alpha_pct = 90;
	int still = 0;   /* --still: one centred pointer at full opacity (docs) */

	for (int i = 5; i < argc; i++) {
		if (!strcmp(argv[i], "--still")) { still = 1; continue; }
		if (i + 1 >= argc) { usage(); return 2; }
		const char *v = argv[i + 1];
		if (!strcmp(argv[i], "--radius")) style.radius = atoi(v);
		else if (!strcmp(argv[i], "--thickness")) style.thickness = atoi(v);
		else if (!strcmp(argv[i], "--glow")) style.glow = atoi(v);
		else if (!strcmp(argv[i], "--alpha")) alpha_pct = atoi(v);
		else if (!strcmp(argv[i], "--color")) style.color = (uint32_t)strtoul(v, NULL, 16);
		else { usage(); return 2; }
		i++;
	}
	if (w <= 0 || h <= 0 || frames <= 0 || frames > 10000) {
		fprintf(stderr, "render-demo: bad frame geometry\n");
		return 2;
	}

	struct canvas c = { .px = calloc((size_t)w * (size_t)h, 4), .w = w, .h = h };
	if (!c.px) { perror("calloc"); return 1; }

	for (int i = 0; i < frames; i++) {
		/* one closed loop: the pointer wanders while the ring is held, fades
		 * out (toggle off), stays hidden, then fades back in (toggle on) — so
		 * frame 0 and the loop point both show the ring at full opacity. */
		double t = 2.0 * M_PI * i / frames;
		double cx = w * (0.5 + 0.30 * cos(t));
		double cy = h * (0.5 + 0.26 * sin(2 * t));

		double u = (double)i / frames;
		double env = 1.0;
		if (u >= 0.55 && u < 0.70) env = 1.0 - (u - 0.55) / 0.15;
		else if (u >= 0.70 && u < 0.85) env = 0.0;
		else if (u >= 0.85) env = (u - 0.85) / 0.15;
		if (still) { cx = w / 2.0; cy = h / 2.0; env = 1.0; }
		double alpha = alpha_pct / 100.0 * env;

		memset(c.px, 0, (size_t)w * (size_t)h * 4);
		/* ring_draw works in buffer px; the demo frame is 1:1, so the ring
		 * is drawn at 2x the logical radius, as on a 2x surface. */
		int ext = ring_extent(&style) * 2; /* frame px are buffer px */
		int x1 = (int)cx - ext, y1 = (int)cy - ext;
		int x2 = (int)cx + ext, y2 = (int)cy + ext;
		if (x1 < 0) x1 = 0;
		if (y1 < 0) y1 = 0;
		if (x2 > w) x2 = w;
		if (y2 > h) y2 = h;
		ring_draw(c.px, w, &style, cx, cy, alpha, x1, y1, x2, y2);
		draw_pointer(&c, cx, cy);

		char path[4096];
		snprintf(path, sizeof path, "%s/f%04d.rgba", outdir, i);
		if (write_rgba(path, &c) < 0) {
			fprintf(stderr, "render-demo: %s: %s\n", path, strerror(errno));
			free(c.px);
			return 1;
		}
	}

	free(c.px);
	printf("wrote %d frames to %s\n", frames, outdir);
	return 0;
}
