WAYLAND := $(shell pkg-config --cflags wayland-client) -I.
LDLIBS := $(shell pkg-config --libs wayland-client) -lm

CFLAGS ?= -O2 -std=c11 -Wall -Wextra
PROTO := ../protocol

all: ringworm

layer-shell.h: $(PROTO)/wlr-layer-shell-unstable-v1.xml
	wayland-scanner client-header $< $@

layer-shell.c: $(PROTO)/wlr-layer-shell-unstable-v1.xml
	wayland-scanner private-code $< $@

xdg-shell.c: $(PROTO)/xdg-shell.xml
	wayland-scanner private-code $< $@

# xdg-shell.c is linked only for the wl_interface symbols referenced by
# layer-shell's get_popup argument table; none of its code paths run.
ringworm: src/main.c layer-shell.c layer-shell.h xdg-shell.c
	$(CC) $(CFLAGS) $(WAYLAND) -o $@ src/main.c layer-shell.c xdg-shell.c $(LDLIBS)

clean:
	rm -f ringworm layer-shell.h layer-shell.c

.PHONY: all clean
