/* ringworm — accessibility ring around the cursor for niri.
 *
 * SPDX-License-Identifier: MIT
 *
 * A click-through wlr-layer-shell overlay drawn on the top layer of every
 * output, fed by niri's IPC event stream (requires the cursor-position patch
 * in ../niri-patch/). Toggle with `ringworm toggle`.
 *
 * Design invariants (docs/adr/):
 *  - empty input region: clicks always pass through
 *  - positions arrive as event-stream push; never polled
 *  - a feed that goes silent for >3s (missed heartbeats) fades the ring
 *    instead of freezing it mid-screen; a resting cursor is not staleness
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "layer-shell.h"
#include "wayland-client.h"

/* ------------------------------------------------------------------ */
/* config                                                              */

/* logging: kept above config because cfg_load reports */
static int g_verbose;
#define logv(...) do { if (g_verbose) fprintf(stderr, "ringworm: " __VA_ARGS__); } while (0)
#define loge(...) do { fprintf(stderr, "ringworm: " __VA_ARGS__); } while (0)

/* runtime state flags shared with the config parser */
static bool g_enabled = false;    /* ring hidden until invoked (config/keybind) */
static bool g_fake;               /* --fake: sine-wave demo mode */

struct config {
	int radius;      /* logical px to the ring centerline */
	int thickness;   /* logical px */
	int glow;        /* logical px, soft falloff outside the ring */
	uint32_t color;  /* 0xRRGGBB */
	int alpha;       /* percent of full opacity */
	int fade_ms;     /* fade in/out duration */
};

static struct config cfg = {
	.radius = 26, .thickness = 5, .glow = 9,
	.color = 0xffb000, .alpha = 90, .fade_ms = 150,
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void cfg_apply(const char *key, const char *val) {
	if (!strcmp(key, "radius")) cfg.radius = clampi(atoi(val), 4, 400);
	else if (!strcmp(key, "thickness")) cfg.thickness = clampi(atoi(val), 1, 100);
	else if (!strcmp(key, "glow")) cfg.glow = clampi(atoi(val), 0, 200);
	else if (!strcmp(key, "alpha")) cfg.alpha = clampi(atoi(val), 1, 100);
	else if (!strcmp(key, "fade_ms")) cfg.fade_ms = clampi(atoi(val), 0, 2000);
	else if (!strcmp(key, "enabled")) {
		/* the ring stays hidden after boot unless the config or the keybind
		 * turns it on; accepting several spellings keeps the file friendly */
		g_enabled = !strcmp(val, "1") || !strcasecmp(val, "yes") ||
			!strcasecmp(val, "true") || !strcasecmp(val, "on");
	}
	else if (!strcmp(key, "color")) {
		char *end = NULL;
		unsigned long v = strtoul(val, &end, 16);
		if (end && *end == '\0' && end > val) cfg.color = (uint32_t)(v & 0xffffff);
	}
}

static void cfg_load(void) {
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[512];
	if (xdg && *xdg)
		snprintf(path, sizeof path, "%s/ringworm/ringworm.conf", xdg);
	else if (home && *home)
		snprintf(path, sizeof path, "%s/.config/ringworm/ringworm.conf", home);
	else
		return;

	FILE *f = fopen(path, "r");
	if (!f) {
		logv("no config at %s, using defaults\n", path);
		return;
	}
	char line[256];
	while (fgets(line, sizeof line, f)) {
		char *hash = strchr(line, '#');
		if (hash) *hash = '\0';
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = line, *val = eq + 1;
		while (isspace((unsigned char)*key)) key++;
		char *e = key + strlen(key);
		while (e > key && isspace((unsigned char)e[-1])) *--e = '\0';
		while (isspace((unsigned char)*val)) val++;
		e = val + strlen(val);
		while (e > val && isspace((unsigned char)e[-1])) *--e = '\0';
		if (*key && *val) cfg_apply(key, val);
	}
	fclose(f);
	logv("config loaded from %s\n", path);
}

/* ------------------------------------------------------------------ */
/* logging, time                                                       */

static double mono_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ------------------------------------------------------------------ */
/* tiny JSON reader — enough for niri's machine-generated output       */

static const char *js_ws(const char *s) {
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
	return s;
}

static const char *js_skip_string(const char *s) {
	s++; /* opening quote */
	while (*s && *s != '"') {
		if (*s == '\\' && s[1]) s++;
		s++;
	}
	return *s ? s + 1 : s;
}

static const char *js_skip(const char *s) {
	s = js_ws(s);
	if (*s == '"') return js_skip_string(s);
	if (*s == '[' || *s == '{') {
		char open = *s, close = open == '[' ? ']' : '}';
		int depth = 0;
		while (*s) {
			if (*s == '"') { s = js_skip_string(s); continue; }
			if (*s == open) depth++;
			else if (*s == close) { if (--depth == 0) return s + 1; }
			s++;
		}
		return s;
	}
	/* number / literal */
	while (*s && *s != ',' && *s != '}' && *s != ']' && *s != ' ' && *s != '\n') s++;
	return s;
}

/* Given s pointing at '{', return the value of its top-level `key`, or NULL. */
static const char *js_field(const char *obj, const char *key) {
	const char *s = js_ws(obj);
	if (*s != '{') return NULL;
	s = js_ws(s + 1);
	size_t klen = strlen(key);
	while (*s && *s != '}') {
		if (*s != '"') return NULL;
		const char *k = s + 1;
		const char *end = strchr(k, '"');
		if (!end) return NULL;
		s = js_ws(end + 1);
		if (*s != ':') return NULL;
		s = js_ws(s + 1);
		if ((size_t)(end - k) == klen && !strncmp(k, key, klen)) return s;
		s = js_ws(js_skip(s));
		if (*s == ',') { s = js_ws(s + 1); continue; }
		return NULL;
	}
	return NULL;
}

static bool js_pair(const char *arr, double *x, double *y) {
	arr = js_ws(arr);
	if (*arr != '[') return false;
	char *end = NULL;
	*x = strtod(arr + 1, &end);
	if (end == arr + 1) return false;
	arr = js_ws(end);
	if (*arr != ',') return false;
	*y = strtod(arr + 1, &end);
	if (end == arr + 1) return false;
	return true;
}

/* ------------------------------------------------------------------ */
/* shared runtime state                                                */

static double g_alpha = 0.0;      /* animated 0..1 */
static double g_px, g_py;         /* latest global logical position */
static bool g_pos_valid;
static double g_last_render_ms = -1e9;
static bool g_force_render = true;

static volatile sig_atomic_t sig_toggle, sig_reload, sig_quit;

/* ------------------------------------------------------------------ */
/* wayland                                                             */

struct wbuf {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	int32_t w, h;
	bool busy;
};

struct output {
	struct wl_output *wl;
	uint32_t global_name;
	int32_t x, y;            /* logical, global compositor space */
	int32_t scale;
	bool configured;
	int32_t w, h;            /* logical size from configure */
	struct wl_surface *sfc;
	struct zwlr_layer_surface_v1 *ls;
	struct wbuf buf[2];
	int cur;
	bool has_last[2];
	int last[2][4];          /* drawn bbox per buffer, buffer px */
	struct output *next;
};

static struct wl_display *disp;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *lshell;
static struct wl_region *empty_region;
static struct output *outputs;
static int output_count;

static void buffer_release(void *data, struct wl_buffer *wl) {
	(void)wl;
	((struct wbuf *)data)->busy = false;
}
static const struct wl_buffer_listener buf_listener = { buffer_release };

static void buffers_free(struct output *o) {
	for (int i = 0; i < 2; i++) {
		if (o->buf[i].wl) { wl_buffer_destroy(o->buf[i].wl); o->buf[i].wl = NULL; }
		if (o->buf[i].data) { munmap(o->buf[i].data, o->buf[i].size); o->buf[i].data = NULL; }
	}
	/* pool lives implicitly on the compositor side; we never resize it */
}

static bool buffers_alloc(struct output *o, int32_t w, int32_t h) {
	size_t size = (size_t)w * h * 4;
	/* one pool, two disjoint regions: front at 0, back at size */
	int fd = memfd_create("ringworm", MFD_CLOEXEC);
	if (fd < 0) return false;
	if (ftruncate(fd, (off_t)(size * 2)) < 0) { close(fd); return false; }
	void *data = mmap(NULL, size * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) { close(fd); return false; }
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)(size * 2));
	close(fd); /* libwayland holds its own reference */
	if (!pool) { munmap(data, size * 2); return false; }

	memset(data, 0, size * 2);
	for (int i = 0; i < 2; i++) {
		o->buf[i].data = (char *)data + (size_t)i * size;
		o->buf[i].size = size;
		o->buf[i].w = w;
		o->buf[i].h = h;
		o->buf[i].busy = false;
		o->buf[i].wl = wl_shm_pool_create_buffer(pool, (int32_t)(i * size), w, h,
			w * 4, WL_SHM_FORMAT_ARGB8888);
		wl_buffer_add_listener(o->buf[i].wl, &buf_listener, &o->buf[i]);
	}
	wl_shm_pool_destroy(pool);
	o->cur = 0;
	o->has_last[0] = o->has_last[1] = false;
	logv("output buffers %dx%d x2 (scale 2)\n", w, h);
	return true;
}

static void clear_rect(struct wbuf *b, int x1, int y1, int x2, int y2) {
	for (int y = y1; y < y2; y++) {
		uint32_t *row = (uint32_t *)b->data + (size_t)y * b->w;
		memset(row + x1, 0, (size_t)(x2 - x1) * 4);
	}
}

static void draw_ring(struct wbuf *b, double cx, double cy,
	int x1, int y1, int x2, int y2, double alpha) {
	if (alpha <= 0.004) return;
	double rr = cfg.radius * 2.0, tt = cfg.thickness * 2.0, gg = cfg.glow * 2.0;
	if (gg < 1.0) gg = 1.0;
	double cr = ((cfg.color >> 16) & 255) / 255.0;
	double cg = ((cfg.color >> 8) & 255) / 255.0;
	double cb = (cfg.color & 255) / 255.0;
	double inv2g2 = 1.0 / (2.0 * gg * gg);

	for (int y = y1; y < y2; y++) {
		uint32_t *row = (uint32_t *)b->data + (size_t)y * b->w;
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

static void render_output(struct output *o) {
	if (!o->configured || !o->buf[0].wl) return;
	int i = o->cur;
	struct wbuf *b = &o->buf[i];
	if (b->busy) return;

	double A = g_alpha * cfg.alpha / 100.0;
	double cx = (g_px - o->x) * 2.0;
	double cy = (g_py - o->y) * 2.0;
	int half = (cfg.radius + cfg.thickness + cfg.glow + 3) * 2;
	int nx1 = (int)floor(cx) - half, ny1 = (int)floor(cy) - half;
	int nx2 = (int)floor(cx) + half + 1, ny2 = (int)floor(cy) + half + 1;

	int ox1 = 0, oy1 = 0, ox2 = -1, oy2 = -1;
	if (o->has_last[i]) {
		ox1 = o->last[i][0]; oy1 = o->last[i][1];
		ox2 = o->last[i][2]; oy2 = o->last[i][3];
		clear_rect(b, ox1, oy1, ox2, oy2);
	}

	int ax1 = nx1 < 0 ? 0 : nx1, ay1 = ny1 < 0 ? 0 : ny1;
	int ax2 = nx2 > b->w ? b->w : nx2, ay2 = ny2 > b->h ? b->h : ny2;
	bool drew = false;
	if (ax2 > ax1 && ay2 > ay1 && A > 0.004) {
		draw_ring(b, cx, cy, ax1, ay1, ax2, ay2, A);
		drew = true;
	}

	if (ox2 > ox1)
		wl_surface_damage_buffer(o->sfc, ox1, oy1, ox2 - ox1, oy2 - oy1);
	if (drew)
		wl_surface_damage_buffer(o->sfc, ax1, ay1, ax2 - ax1, ay2 - ay1);

	if (drew) {
		o->last[i][0] = ax1; o->last[i][1] = ay1;
		o->last[i][2] = ax2; o->last[i][3] = ay2;
		o->has_last[i] = true;
	} else {
		o->has_last[i] = false;
	}

	if (drew || ox2 > ox1) {
		wl_surface_attach(o->sfc, b->wl, 0, 0);
		wl_surface_commit(o->sfc);
		b->busy = true;
		o->cur = i ^ 1;
	}
}

static void render_all(void) {
	for (struct output *o = outputs; o; o = o->next) render_output(o);
	g_last_render_ms = mono_ms();
}

/* --- output (wl_output) events --- */

static void out_geometry(void *data, struct wl_output *wl, int32_t x, int32_t y,
	int32_t pw, int32_t ph, int32_t sub, const char *make, const char *model,
	int32_t transform) {
	(void)wl; (void)pw; (void)ph; (void)sub; (void)make; (void)model; (void)transform;
	struct output *o = data;
	o->x = x; o->y = y;
	logv("output geometry: %d,%d\n", x, y);
}
static void out_mode(void *data, struct wl_output *wl, uint32_t flags,
	int32_t w, int32_t h, int32_t refresh) {
	(void)data; (void)wl; (void)flags; (void)w; (void)h; (void)refresh;
}
static void out_done(void *data, struct wl_output *wl) { (void)data; (void)wl; }
static void out_scale(void *data, struct wl_output *wl, int32_t scale) {
	(void)wl;
	struct output *o = data;
	o->scale = scale;
}
static void out_name(void *data, struct wl_output *wl, const char *name) {
	(void)data; (void)wl;
	logv("output: %s\n", name);
}
static void out_description(void *data, struct wl_output *wl, const char *description) {
	(void)data; (void)wl; (void)description;
}
static const struct wl_output_listener out_listener = {
	.geometry = out_geometry, .mode = out_mode, .done = out_done,
	.scale = out_scale, .name = out_name, .description = out_description,
};

/* --- layer surface events --- */

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
	uint32_t serial, uint32_t w, uint32_t h) {
	struct output *o = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	int32_t nw = (int32_t)w, nh = (int32_t)h;
	if (!o->configured || nw != o->w || nh != o->h) {
		o->w = nw; o->h = nh; o->configured = true;
		buffers_free(o);
		if (!buffers_alloc(o, w * 2, h * 2)) {
			loge("buffer allocation failed\n");
			return;
		}
		g_force_render = true;
	}
}
static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	struct output *o = data;
	(void)ls;
	logv("layer surface closed\n");
	zwlr_layer_surface_v1_destroy(o->ls); o->ls = NULL;
	wl_surface_destroy(o->sfc); o->sfc = NULL;
	buffers_free(o);
	o->configured = false;
}
static const struct zwlr_layer_surface_v1_listener ls_listener = {
	.configure = ls_configure, .closed = ls_closed,
};

static void output_create_surface(struct output *o) {
	o->sfc = wl_compositor_create_surface(comp);
	wl_surface_set_input_region(o->sfc, empty_region);
	wl_surface_set_buffer_scale(o->sfc, 2);
	o->ls = zwlr_layer_shell_v1_get_layer_surface(lshell, o->sfc, o->wl,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "ringworm");
	zwlr_layer_surface_v1_set_anchor(o->ls,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(o->ls, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(o->ls,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	zwlr_layer_surface_v1_add_listener(o->ls, &ls_listener, o);
	wl_surface_commit(o->sfc);
}

static void output_clear_commit(struct output *o) {
	struct wbuf *b = &o->buf[o->cur];
	if (!b->wl || b->busy) return;
	memset(b->data, 0, b->size);
	o->has_last[o->cur] = false;
	wl_surface_attach(o->sfc, b->wl, 0, 0);
	wl_surface_damage_buffer(o->sfc, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(o->sfc);
	b->busy = true;
	o->cur ^= 1;
}

/* --- registry --- */

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
	const char *iface, uint32_t version) {
	(void)data;
	if (!strcmp(iface, wl_compositor_interface.name)) {
		comp = wl_registry_bind(reg, name, &wl_compositor_interface, version < 4 ? version : 4);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
		lshell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface,
			version < 3 ? version : 3);
	} else if (!strcmp(iface, wl_output_interface.name)) {
		struct output *o = calloc(1, sizeof *o);
		o->global_name = name;
		o->scale = 1;
		o->wl = wl_registry_bind(reg, name, &wl_output_interface, version < 4 ? version : 4);
		wl_output_add_listener(o->wl, &out_listener, o);
		o->next = outputs;
		outputs = o;
		output_count++;
	}
}
static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)data; (void)reg;
	for (struct output **p = &outputs; *p; p = &(*p)->next) {
		if ((*p)->global_name == name) {
			struct output *o = *p;
			*p = o->next;
			if (o->ls) zwlr_layer_surface_v1_destroy(o->ls);
			if (o->sfc) wl_surface_destroy(o->sfc);
			buffers_free(o);
			wl_output_destroy(o->wl);
			output_count--;
			free(o);
			logv("output removed (global %u)\n", name);
			return;
		}
	}
}
static const struct wl_registry_listener registry_listener = {
	.global = registry_global, .global_remove = registry_global_remove,
};

/* ------------------------------------------------------------------ */
/* unix sockets                                                        */

/* sockaddr_un.sun_path holds at most 107 chars + NUL; reject longer. */
static bool saun_fill(struct sockaddr_un *addr, const char *path) {
	if (strlen(path) >= sizeof addr->sun_path) return false;
	addr->sun_family = AF_UNIX;
	strcpy(addr->sun_path, path);
	return true;
}

/* ------------------------------------------------------------------ */
/* niri IPC feed                                                       */

static int ipc_fd = -1;
static double ipc_retry_at = 0;
static bool ipc_stream_ready;      /* got the initial reply */
static char ipc_line[8192];
static size_t ipc_line_len;
static int ipc_connect_failures;   /* failed connects before env fallback */

static bool ipc_socket_path(char *path, size_t cap) {
	const char *env = getenv("NIRI_SOCKET");
	/* After repeated failed connects the env path is stale (e.g. compositor
	 * restarted): fall through to discovery instead of retrying it forever. */
	if (env && *env && ipc_connect_failures < 3) { snprintf(path, cap, "%s", env); return true; }
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !*runtime) return false;
	/* scan $XDG_RUNTIME_DIR for niri.*.sock, newest wins */
	DIR *d = opendir(runtime);
	if (!d) return false;
	double best = -1;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "niri.", 5) || strlen(de->d_name) < 10 ||
			strcmp(de->d_name + strlen(de->d_name) - 5, ".sock")) continue;
		char p[512];
		snprintf(p, sizeof p, "%s/%s", runtime, de->d_name);
		struct stat st;
		if (stat(p, &st) == 0 && (double)st.st_mtime > best) {
			best = (double)st.st_mtime;
			snprintf(path, cap, "%s", p);
		}
	}
	closedir(d);
	return best >= 0;
}

static void ipc_disconnect(void) {
	if (ipc_fd >= 0) close(ipc_fd);
	ipc_fd = -1;
	ipc_stream_ready = false;
	ipc_line_len = 0;
	ipc_retry_at = mono_ms() + 1000;
}

static void ipc_connect(void) {
	if (ipc_fd >= 0 || mono_ms() < ipc_retry_at) return;
	char path[512];
	if (!ipc_socket_path(path, sizeof path)) {
		ipc_retry_at = mono_ms() + 1000;
		return;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0) return;
	struct sockaddr_un addr;
	if (!saun_fill(&addr, path)) {
		loge("ipc socket path too long: %s\n", path);
		ipc_retry_at = mono_ms() + 1000;
		return;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		if (++ipc_connect_failures % 5 == 1)
			logv("ipc connect to %s failed: %s\n", path, strerror(errno));
		close(fd);
		ipc_retry_at = mono_ms() + 1000;
		return;
	}
	ipc_fd = fd;
	ipc_stream_ready = false;
	ipc_line_len = 0;
	static const char req[] = "\"CursorPosition\"\n";
	write(fd, req, sizeof req - 1);
	logv("ipc connected: %s\n", path);
}

static void ipc_process_line(const char *line) {
	if (strstr(line, "CursorPositionChanged")) {
		const char *ev = js_field(line, "CursorPositionChanged");
		if (ev) {
			const char *p = js_field(ev, "position");
			if (p && js_pair(p, &g_px, &g_py)) {
				g_pos_valid = true;
				return;
			}
		}
	} else if (!ipc_stream_ready && strstr(line, "\"Ok\"")) {
		const char *ok = js_field(line, "Ok");
		if (ok) {
			const char *p = js_field(ok, "CursorPosition");
			if (p && js_pair(p, &g_px, &g_py)) {
				g_pos_valid = true;
				ipc_stream_ready = true;
				logv("ipc position %.1f,%.1f; subscribing to event stream\n", g_px, g_py);
				static const char sub[] = "\"EventStream\"\n";
				ssize_t rc = write(ipc_fd, sub, sizeof sub - 1);
				(void)rc;
				return;
			}
		}
	} else if (strstr(line, "\"Err\"")) {
		loge("ipc error reply: %s", line);
		ipc_disconnect();
		return;
	}
	/* other lines (state snapshots, unrelated events) are ignored */
}

static void ipc_pump(void) {
	if (ipc_fd < 0) { ipc_connect(); return; }
	/* NOTE: no heartbeats. niri stops reading requests from event-stream
	 * clients once subscribed, so anything we write piles up until the send
	 * buffer wedges (~5 min of pings). The event stream itself is the
	 * liveness signal: a dead compositor closes the socket and the read
	 * loop sees EOF immediately. */
	char tmp[4096];
	ssize_t n;
	while ((n = read(ipc_fd, tmp, sizeof tmp)) > 0) {
		for (ssize_t i = 0; i < n; i++) {
			if (tmp[i] == '\n') {
				ipc_line[ipc_line_len < sizeof ipc_line - 1 ? ipc_line_len : sizeof ipc_line - 1] = '\0';
				if (ipc_line_len) ipc_process_line(ipc_line);
				ipc_line_len = 0;
			} else if (ipc_line_len < sizeof ipc_line - 1) {
				ipc_line[ipc_line_len++] = tmp[i];
			}
		}
	}
	if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
		logv("ipc feed closed\n");
		ipc_disconnect();
	}
}

/* ------------------------------------------------------------------ */
/* control socket                                                      */

static int ctl_fd = -1;
static char ctl_path[512];

static void ctl_listen(void) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !*runtime) {
		loge("XDG_RUNTIME_DIR not set; toggle socket disabled\n");
		return;
	}
	snprintf(ctl_path, sizeof ctl_path, "%s/ringworm.sock", runtime);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0) return;

	struct sockaddr_un addr;
	if (!saun_fill(&addr, ctl_path)) {
		loge("control socket path too long: %s\n", ctl_path);
		close(fd);
		return;
	}
	if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		if (errno == EADDRINUSE) {
			/* someone else's socket: if it answers, we are the second copy */
			int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
			if (probe >= 0) {
				if (connect(probe, (struct sockaddr *)&addr, sizeof addr) == 0) {
					close(probe);
					loge("another ringworm daemon is already running\n");
					exit(0);
				}
				close(probe);
			}
			unlink(ctl_path);
			if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
				loge("cannot bind control socket: %s\n", strerror(errno));
				close(fd);
				return;
			}
		} else {
			loge("cannot bind control socket: %s\n", strerror(errno));
			close(fd);
			return;
		}
	}
	if (listen(fd, 4) < 0) { close(fd); return; }
	ctl_fd = fd;
	logv("control socket: %s\n", ctl_path);
}

/* append nul-safe reply into buf */
static void ctl_reply(int fd, const char *text) {
	size_t len = strlen(text);
	ssize_t rc = write(fd, text, len);
	(void)rc;
}

static void ctl_pump(void) {
	if (ctl_fd < 0) return;
	int c = accept(ctl_fd, NULL, NULL);
	if (c < 0) return;
	char cmd[64] = { 0 };
	ssize_t n = read(c, cmd, sizeof cmd - 1);
	if (n <= 0) { close(c); return; }
	cmd[strcspn(cmd, "\r\n")] = '\0';

	if (!strcmp(cmd, "toggle")) {
		g_enabled = !g_enabled;
		ctl_reply(c, g_enabled ? "ring on\n" : "ring off\n");
	} else if (!strcmp(cmd, "on")) {
		g_enabled = true;
		ctl_reply(c, "ring on\n");
	} else if (!strcmp(cmd, "off")) {
		g_enabled = false;
		ctl_reply(c, "ring off\n");
	} else if (!strcmp(cmd, "reload")) {
		cfg_load();
		g_force_render = true;
		ctl_reply(c, "reloaded\n");
	} else if (!strcmp(cmd, "status")) {
		char buf[256];
		snprintf(buf, sizeof buf, "state=%s feed=%s pos=%.1f,%.1f outputs=%d\n",
			g_enabled ? "on" : "off",
			ipc_fd >= 0 ? "live" : "disconnected",
			g_px, g_py, output_count);
		ctl_reply(c, buf);
	} else if (!strcmp(cmd, "stop")) {
		ctl_reply(c, "bye\n");
		sig_quit = 1;
	} else {
		ctl_reply(c, "err: unknown command (toggle|on|off|status|reload|stop)\n");
	}
	close(c);
}

/* ------------------------------------------------------------------ */
/* one-shot client mode                                                */

static int client_main(const char *cmd) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !*runtime) {
		loge("XDG_RUNTIME_DIR not set\n");
		return 1;
	}
	char path[512];
	snprintf(path, sizeof path, "%s/ringworm.sock", runtime);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) { loge("socket: %s\n", strerror(errno)); return 1; }
	struct sockaddr_un addr;
	if (!saun_fill(&addr, path)) {
		loge("socket path too long: %s\n", path);
		close(fd);
		return 1;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		loge("daemon not running (%s)\n", path);
		close(fd);
		return 1;
	}
	dprintf(fd, "%s\n", cmd);
	struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	char reply[256];
	ssize_t n = read(fd, reply, sizeof reply - 1);
	if (n > 0) {
		reply[n] = '\0';
		fputs(reply, stdout);
	}
	close(fd);
	return 0;
}

/* ------------------------------------------------------------------ */

static void on_signal(int s) {
	if (s == SIGUSR1) sig_toggle = 1;
	else if (s == SIGHUP) sig_reload = 1;
	else sig_quit = 1;
}

static void usage(FILE *f) {
	fputs(
		"ringworm — accessibility ring around the cursor (niri)\n\n"
		"  ringworm            run the daemon\n"
		"  ringworm -v         run the daemon with verbose logging\n"
		"  ringworm toggle|on|off|status|reload|stop   control the daemon\n",
		f);
}

int main(int argc, char **argv) {
	/* client mode? */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(stdout); return 0; }
		if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) { g_verbose = 1; continue; }
		if (!strcmp(argv[i], "--fake")) { g_fake = true; g_enabled = true; continue; }
		if (!strcmp(argv[i], "toggle") || !strcmp(argv[i], "on") || !strcmp(argv[i], "off") ||
			!strcmp(argv[i], "status") || !strcmp(argv[i], "reload") || !strcmp(argv[i], "stop"))
			return client_main(argv[i]);
		loge("unknown argument: %s\n", argv[i]);
		usage(stderr);
		return 1;
	}

	cfg_load();

	struct sigaction sa = { .sa_handler = on_signal };
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	disp = wl_display_connect(NULL);
	if (!disp) { loge("cannot connect to Wayland display\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(disp);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(disp);
	if (!comp || !shm || !lshell) {
		loge("compositor lacks %s%s%s (wlr-layer-shell is required)\n",
			comp ? "" : "wl_compositor ", shm ? "" : "wl_shm ",
			lshell ? "" : "zwlr_layer_shell_v1");
		return 1;
	}
	empty_region = wl_compositor_create_region(comp);
	wl_region_add(empty_region, 0, 0, 0, 0);

	for (struct output *o = outputs; o; o = o->next) output_create_surface(o);
	wl_display_roundtrip(disp); /* picks up configure events */
	for (struct output *o = outputs; o; o = o->next)
		if (o->configured) output_clear_commit(o);
	wl_display_flush(disp);
	logv("%d outputs overlaid\n", output_count);

	ctl_listen();
	ipc_connect();

	double last_tick = mono_ms();
	while (!sig_quit) {
		if (sig_toggle) {
			sig_toggle = 0;
			g_enabled = !g_enabled;
			logv("toggled -> %s\n", g_enabled ? "on" : "off");
		}
		if (sig_reload) {
			sig_reload = 0;
			cfg_load();
			g_force_render = true;
		}

		double now = mono_ms();
		double dt = now - last_tick;
		last_tick = now;

		ipc_pump();
		ctl_pump();

		if (g_fake) {
			double t = now / 1000.0;
			g_px = 960 + cos(t * 1.7) * 420.0;
			g_py = 540 + sin(t * 1.3) * 260.0;
			g_pos_valid = true;
			g_force_render = true;
		}

		/* outputs added later (hotplug) get their layer surfaces here */
		bool need_flush = false;
		for (struct output *o = outputs; o; o = o->next) {
			if (!o->sfc) {
				output_create_surface(o);
				need_flush = true;
			}
		}
		if (need_flush) wl_display_flush(disp);

		/* liveness = the connection itself: EOF on compositor death, no
		 * time-based staleness. A resting cursor must not fade the ring. */
		double target = (g_enabled && g_pos_valid && ipc_fd >= 0) ? 1.0 : 0.0;
		double prev = g_alpha;
		if (cfg.fade_ms <= 0) {
			g_alpha = target;
		} else {
			double step = dt / cfg.fade_ms;
			if (g_alpha < target) g_alpha = g_alpha + step > target ? target : g_alpha + step;
			else g_alpha = g_alpha - step < target ? target : g_alpha - step;
		}

		if (g_force_render || fabs(g_alpha - prev) > 0.004 ||
			(g_alpha > 0 && now - g_last_render_ms >= 7.0)) {
			g_force_render = false;
			render_all();
			wl_display_flush(disp);
		}

		struct pollfd fds[3] = {
			{ .fd = wl_display_get_fd(disp), .events = POLLIN },
			{ .fd = ctl_fd >= 0 ? ctl_fd : -1, .events = POLLIN },
			{ .fd = ipc_fd >= 0 ? ipc_fd : -1, .events = POLLIN },
		};
		int r = poll(fds, 3, 4);
		if (r < 0 && errno != EINTR) break;

		if (fds[0].revents & POLLIN) wl_display_dispatch(disp);
		else wl_display_dispatch_pending(disp);
		if (fds[0].revents & (POLLERR | POLLHUP)) {
			loge("wayland display died\n");
			break;
		}
	}

	logv("shutting down\n");
	if (ctl_fd >= 0) { close(ctl_fd); unlink(ctl_path); }
	return 0;
}
