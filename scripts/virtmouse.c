/* virtmouse — inject absolute pointer motion into a compositor that
 * implements zwlr_virtual_pointer (niri does). Verification tool: drives the
 * cursor without a human so acceptance tests can run unattended.
 *
 * SPDX-License-Identifier: MIT
 *
 * usage: virtmouse <x> <y> [x_extent] [y_extent]
 * extent defaults to 1000x1000; coordinates are within that extent.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "virtual-pointer.h"
#include "wayland-client.h"

static struct zwlr_virtual_pointer_manager_v1 *vp_mgr;
static struct zwlr_virtual_pointer_v1 *vp;

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
	const char *iface, uint32_t version) {
	(void)data;
	if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name)) {
		vp_mgr = wl_registry_bind(reg, name,
			&zwlr_virtual_pointer_manager_v1_interface,
			version < 2 ? version : 2);
	}
}
static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)data; (void)reg; (void)name;
}
static const struct wl_registry_listener registry_listener = {
	.global = registry_global, .global_remove = registry_global_remove,
};

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: virtmouse <x> <y> [x_extent] [y_extent]\n");
		return 1;
	}
	int x = atoi(argv[1]), y = atoi(argv[2]);
	int xe = argc > 3 ? atoi(argv[3]) : 1000;
	int ye = argc > 4 ? atoi(argv[4]) : 1000;

	struct wl_display *disp = wl_display_connect(NULL);
	if (!disp) { fprintf(stderr, "virtmouse: no wayland display\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(disp);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(disp);
	if (!vp_mgr) {
		fprintf(stderr, "virtmouse: compositor lacks zwlr_virtual_pointer\n");
		return 1;
	}

	vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(vp_mgr, NULL);
	zwlr_virtual_pointer_v1_motion_absolute(vp, 0, (uint32_t)x, (uint32_t)y,
		(uint32_t)xe, (uint32_t)ye);
	zwlr_virtual_pointer_v1_frame(vp);
	wl_display_flush(disp);
	wl_display_roundtrip(disp);

	zwlr_virtual_pointer_v1_destroy(vp);
	zwlr_virtual_pointer_manager_v1_destroy(vp_mgr);
	wl_display_disconnect(disp);
	return 0;
}
